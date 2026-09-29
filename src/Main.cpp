// SPDX-License-Identifier: AGPL-3.0-or-later
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <numeric>

namespace
{
constexpr std::array<double, 7> clickTimes { 0.65, 1.03, 1.51, 2.08, 2.54, 3.11, 3.47 };
constexpr double captureSeconds = 4.2;

struct CalibrationResult
{
    bool ok = false;
    double latencyMs = 0.0;
    double scatterMs = 0.0;
    double confidence = 0.0;
    juce::String detail;
};

class CalibrationEngine final : public juce::AudioIODeviceCallback,
                                private juce::AsyncUpdater
{
public:
    std::function<void(CalibrationResult)> onFinished;
    std::function<void(float, float)> onMeters;

    void start()
    {
        if (sampleRate.load() <= 0.0)
            return;
        const auto count = static_cast<size_t>(std::ceil(captureSeconds * sampleRate.load()));
        capture.assign(count, 0.0f);
        writePosition.store(0);
        peakIn.store(0.0f);
        peakOut.store(0.0f);
        running.store(true);
    }

    bool isRunning() const noexcept { return running.load(); }
    float getInputPeak() const noexcept { return peakIn.load(); }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        sampleRate.store(device->getCurrentSampleRate());
        inputLatency.store(device->getInputLatencyInSamples());
        outputLatency.store(device->getOutputLatencyInSamples());
        writePosition.store(0);
        running.store(false);
    }

    void audioDeviceStopped() override { running.store(false); }

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear(outputs[ch], numSamples);

        if (!running.load())
            return;

        const auto sr = sampleRate.load();
        auto pos = writePosition.load();
        float blockIn = 0.0f;
        float blockOut = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto absolute = pos + static_cast<size_t>(i);
            float input = 0.0f;
            if (numInputs > 0 && inputs[0] != nullptr)
                input = inputs[0][i];
            if (absolute < capture.size())
                capture[absolute] = input;
            blockIn = std::max(blockIn, std::abs(input));

            float click = 0.0f;
            for (double clickTime : clickTimes)
            {
                const auto start = static_cast<long long>(std::llround(clickTime * sr));
                const auto offset = static_cast<long long>(absolute) - start;
                const auto length = static_cast<long long>(std::llround(0.008 * sr));
                if (offset >= 0 && offset < length)
                {
                    const auto envelope = 1.0 - static_cast<double>(offset) / static_cast<double>(length);
                    click += static_cast<float>(0.22 * envelope * std::sin(juce::MathConstants<double>::twoPi * 1800.0 * offset / sr));
                }
            }
            blockOut = std::max(blockOut, std::abs(click));
            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr)
                    outputs[ch][i] = click;
        }

        peakIn.store(std::max(peakIn.load() * 0.92f, blockIn));
        peakOut.store(std::max(peakOut.load() * 0.92f, blockOut));
        if (onMeters)
            onMeters(peakIn.load(), peakOut.load());

        pos += static_cast<size_t>(numSamples);
        writePosition.store(pos);
        if (pos >= capture.size())
        {
            running.store(false);
            triggerAsyncUpdate();
        }
    }

private:
    static double median(std::vector<double> values)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        const auto n = values.size();
        return n % 2 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
    }

    CalibrationResult analyse() const
    {
        CalibrationResult result;
        const auto sr = sampleRate.load();
        if (capture.empty() || sr <= 0.0)
        {
            result.detail = "No captured audio.";
            return result;
        }

        // An onset-envelope detector is more robust than raw waveform matching
        // for a laptop speaker/microphone path that changes phase and frequency response.
        std::vector<float> envelope(capture.size());
        const int smooth = std::max(1, static_cast<int>(sr * 0.003));
        double rolling = 0.0;
        for (size_t i = 1; i < capture.size(); ++i)
        {
            const float novelty = std::abs(capture[i] - capture[i - 1]);
            rolling += novelty;
            if (i >= static_cast<size_t>(smooth))
                rolling -= std::abs(capture[i - smooth] - capture[i - smooth - 1]);
            envelope[i] = static_cast<float>(rolling / smooth);
        }

        std::vector<double> offsets;
        std::vector<double> strengths;
        const int minDelay = static_cast<int>(0.005 * sr);
        const int maxDelay = static_cast<int>(0.350 * sr);
        for (double clickTime : clickTimes)
        {
            const auto expected = static_cast<int>(std::llround(clickTime * sr));
            const auto begin = std::max(1, expected + minDelay);
            const auto end = std::min(static_cast<int>(envelope.size()) - 1, expected + maxDelay);
            if (begin >= end) continue;
            auto found = begin;
            for (int i = begin + 1; i < end; ++i)
                if (envelope[static_cast<size_t>(i)] > envelope[static_cast<size_t>(found)]) found = i;
            offsets.push_back(static_cast<double>(found - expected));
            strengths.push_back(envelope[static_cast<size_t>(found)]);
        }

        if (offsets.size() < 4)
        {
            result.detail = "Too few clicks were detected. Raise speaker volume or move the mic closer.";
            return result;
        }
        const auto center = median(offsets);
        std::vector<double> deviations;
        for (auto value : offsets) deviations.push_back(std::abs(value - center));
        const auto mad = median(deviations);
        const auto noise = std::accumulate(envelope.begin(), envelope.end(), 0.0) / envelope.size();
        const auto strength = median(strengths);
        result.latencyMs = center * 1000.0 / sr;
        result.scatterMs = mad * 1000.0 / sr;
        result.confidence = juce::jlimit(0.0, 100.0, 20.0 * std::log10((strength + 1.0e-9) / (noise + 1.0e-9)) * 3.0);
        result.ok = result.scatterMs <= 2.0 && result.confidence >= 25.0;
        result.detail = "Driver reports " + juce::String(inputLatency.load() + outputLatency.load())
                      + " samples; acoustic measurement includes speakers, room, and microphone.";
        return result;
    }

    void handleAsyncUpdate() override
    {
        if (onFinished) onFinished(analyse());
    }

    std::vector<float> capture;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> inputLatency { 0 }, outputLatency { 0 };
    std::atomic<size_t> writePosition { 0 };
    std::atomic<float> peakIn { 0.0f }, peakOut { 0.0f };
    std::atomic<bool> running { false };
};

class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent(juce::String commandLine)
        : autoMode(commandLine.contains("--auto-calibrate"))
    {
        setSize(880, 560);
        title.setText("Simple Recorder — latency proof", juce::dontSendNotification);
        title.setFont(juce::FontOptions(26.0f, juce::Font::bold));
        title.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(title);

        status.setText("Opening audio devices…", juce::dontSendNotification);
        status.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(status);

        calibrate.setButtonText("Calibrate speakers → microphone");
        calibrate.onClick = [this] { beginCalibration(); };
        addAndMakeVisible(calibrate);

        addAndMakeVisible(deviceSelector);
        deviceSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff242735));
        deviceSelector.onChange = [this] { selectNamedSetup(); };

        engine.onFinished = [this](CalibrationResult r)
        {
            lastResult = r;
            attempt++;
            const auto verdict = r.ok ? "PASS" : "NEEDS RETRY";
            status.setText(verdict + juce::String("\nMeasured round trip: ")
                           + juce::String(r.latencyMs, 2) + " ms\nScatter: "
                           + juce::String(r.scatterMs, 2) + " ms\nConfidence: "
                           + juce::String(r.confidence, 0) + "%\n" + r.detail,
                           juce::dontSendNotification);
            calibrate.setEnabled(true);
            calibrate.setButtonText(r.ok ? "Run verification again" : "Retry calibration");
            if (autoMode)
            {
                const auto output = juce::File::getCurrentWorkingDirectory()
                    .getChildFile("calibration-result.json");
                juce::DynamicObject::Ptr object = new juce::DynamicObject();
                object->setProperty("ok", r.ok);
                object->setProperty("latencyMs", r.latencyMs);
                object->setProperty("scatterMs", r.scatterMs);
                object->setProperty("confidence", r.confidence);
                object->setProperty("detail", r.detail);
                object->setProperty("device", deviceManager.getCurrentAudioDevice() != nullptr
                                               ? deviceManager.getCurrentAudioDevice()->getName() : "unknown");
                output.replaceWithText(juce::JSON::toString(juce::var(object.get()), true));
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
            repaint();
        };

        deviceManager.initialise(1, 2, nullptr, true);
        deviceManager.addAudioCallback(&engine);
        populateDevices();
        startTimerHz(20);
    }

    ~MainComponent() override
    {
        deviceManager.removeAudioCallback(&engine);
        deviceManager.closeAudioDevice();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151720));
        g.setColour(juce::Colour(0xff2c3040));
        g.fillRoundedRectangle(24.0f, 170.0f, getWidth() - 48.0f, 118.0f, 14.0f);
        g.setColour(juce::Colour(0xff4bd18b));
        g.fillRoundedRectangle(48.0f, 219.0f, (getWidth() - 96.0f) * inputMeter, 18.0f, 7.0f);
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawText("Laptop microphone input", 48, 185, getWidth() - 96, 28, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText("Calibration plays seven quiet clicks. Avoid touching the laptop while it runs.",
                   48, 250, getWidth() - 96, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        title.setBounds(32, 24, getWidth() - 64, 38);
        deviceSelector.setBounds(32, 80, getWidth() - 64, 36);
        calibrate.setBounds(32, 320, getWidth() - 64, 52);
        status.setBounds(40, 400, getWidth() - 80, 130);
    }

private:
    void populateDevices()
    {
        deviceSelector.clear();
        int id = 1;
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            for (const auto& input : type->getDeviceNames(true))
                for (const auto& output : type->getDeviceNames(false))
                {
                    DeviceChoice choice { type->getTypeName(), input, output };
                    if ((input.containsIgnoreCase("Microphone Array") && output.containsIgnoreCase("Speaker"))
                        || (input.containsIgnoreCase("Focusrite") && output.containsIgnoreCase("Focusrite")))
                    {
                        choices.push_back(choice);
                        deviceSelector.addItem(choice.type + ": " + input + " → " + output, id++);
                    }
                }
        }
        if (!choices.empty())
        {
            auto preferred = 0;
            for (int i = 0; i < static_cast<int>(choices.size()); ++i)
                if (choices[static_cast<size_t>(i)].input.containsIgnoreCase("Microphone Array")) preferred = i;
            deviceSelector.setSelectedItemIndex(preferred, juce::sendNotificationSync);
        }
        else status.setText("No compatible input/output pair found.", juce::dontSendNotification);
    }

    void selectNamedSetup()
    {
        const auto index = deviceSelector.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(index, static_cast<int>(choices.size()))) return;
        const auto& choice = choices[static_cast<size_t>(index)];
        deviceManager.setCurrentAudioDeviceType(choice.type, true);
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.inputDeviceName = choice.input;
        setup.outputDeviceName = choice.output;
        setup.sampleRate = 48000.0;
        setup.bufferSize = 256;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setBit(0);
        setup.outputChannels.setRange(0, 2, true);
        const auto error = deviceManager.setAudioDeviceSetup(setup, true);
        auto* device = deviceManager.getCurrentAudioDevice();
        status.setText(error.isNotEmpty() ? error : ("Ready: " + (device ? device->getName() : choice.input)),
                       juce::dontSendNotification);
    }

    void beginCalibration()
    {
        calibrate.setEnabled(false);
        status.setText("Listening… calibration takes about four seconds.", juce::dontSendNotification);
        engine.start();
    }

    void timerCallback() override
    {
        inputMeter = juce::jlimit(0.0f, 1.0f, std::sqrt(engine.getInputPeak()));
        if (autoMode && !autoStarted && deviceManager.getCurrentAudioDevice() != nullptr)
        {
            autoStarted = true;
            beginCalibration();
        }
        repaint();
    }

    struct DeviceChoice { juce::String type, input, output; };
    juce::AudioDeviceManager deviceManager;
    CalibrationEngine engine;
    juce::Label title, status;
    juce::TextButton calibrate;
    juce::ComboBox deviceSelector;
    std::vector<DeviceChoice> choices;
    CalibrationResult lastResult;
    float inputMeter = 0.0f;
    int attempt = 0;
    bool autoMode = false;
    bool autoStarted = false;
};

class RecorderApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Simple Recorder"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& commandLine) override
    {
        window = std::make_unique<Window>(getApplicationName(), commandLine);
    }
    void shutdown() override { window.reset(); }

private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window(const juce::String& name, const juce::String& commandLine)
            : DocumentWindow(name, juce::Colour(0xff151720), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(commandLine), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
}

START_JUCE_APPLICATION(RecorderApplication)
