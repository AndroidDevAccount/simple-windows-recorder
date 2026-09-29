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
    bool plausible = false;
    double latencyMs = 0.0;
    double driverLatencyMs = 0.0;
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
    std::vector<float> getCapturedAudio() const { return capture; }
    double getSampleRate() const noexcept { return sampleRate.load(); }

    void startSpeakerTest()
    {
        testTonePosition.store(0);
        testToneSamples.store(static_cast<int>(sampleRate.load() * 1.2));
    }

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
            if (running.load() && absolute < capture.size())
                capture[absolute] = input;
            blockIn = std::max(blockIn, std::abs(input));

            float click = 0.0f;
            if (running.load())
            {
                for (double clickTime : clickTimes)
                {
                    const auto start = static_cast<long long>(std::llround(clickTime * sr));
                    const auto offset = static_cast<long long>(absolute) - start;
                    const auto length = static_cast<long long>(std::llround(0.010 * sr));
                    if (offset >= 0 && offset < length)
                    {
                        const auto envelope = 1.0 - static_cast<double>(offset) / static_cast<double>(length);
                        click += static_cast<float>(0.48 * envelope * std::sin(juce::MathConstants<double>::twoPi * 1800.0 * offset / sr));
                    }
                }
            }

            auto remaining = testToneSamples.load();
            if (remaining > 0)
            {
                const auto tonePos = testTonePosition.fetch_add(1);
                const auto fade = std::min(1.0, std::min(tonePos / (0.03 * sr), remaining / (0.03 * sr)));
                click += static_cast<float>(0.32 * fade * std::sin(juce::MathConstants<double>::twoPi * 660.0 * tonePos / sr));
                testToneSamples.fetch_sub(1);
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

        if (running.load())
        {
            pos += static_cast<size_t>(numSamples);
            writePosition.store(pos);
            if (pos >= capture.size())
            {
                running.store(false);
                triggerAsyncUpdate();
            }
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
        result.driverLatencyMs = (inputLatency.load() + outputLatency.load()) * 1000.0 / sr;
        result.scatterMs = mad * 1000.0 / sr;
        result.confidence = juce::jlimit(0.0, 100.0, 20.0 * std::log10((strength + 1.0e-9) / (noise + 1.0e-9)) * 3.0);
        result.ok = result.scatterMs <= 2.0 && result.confidence >= 25.0;
        result.plausible = result.latencyMs >= 3.0
                        && result.latencyMs <= 120.0
                        && result.latencyMs <= std::max(80.0, result.driverLatencyMs * 4.0);
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
    std::atomic<int> testToneSamples { 0 };
    std::atomic<long long> testTonePosition { 0 };
};

class CalibrationWaveformView final : public juce::Component
{
public:
    void setResult(std::vector<float> newCapture, double newSampleRate, double newLatencyMs)
    {
        capture = std::move(newCapture);
        sampleRate = newSampleRate;
        latencyMs = newLatencyMs;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff202431));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
        if (capture.empty() || sampleRate <= 0.0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawText("Run calibration to see the played and recorded waveforms.",
                       getLocalBounds(), juce::Justification::centred);
            return;
        }

        const std::array<juce::String, 3> labels {
            "1. Played by speakers",
            "2. Recorded by microphone (arrived " + juce::String(latencyMs, 1) + " ms late)",
            "3. Proposed fix (recording moved " + juce::String(latencyMs, 1) + " ms earlier)"
        };
        constexpr double viewStart = 0.45;
        constexpr double viewEnd = 3.90;
        const auto width = static_cast<float>(getWidth() - 34);
        const auto left = 17.0f;
        const auto rowHeight = static_cast<float>(getHeight()) / 3.0f;

        for (int row = 0; row < 3; ++row)
        {
            const auto top = row * rowHeight;
            const auto centre = top + rowHeight * 0.62f;
            g.setColour(juce::Colours::white.withAlpha(0.72f));
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(labels[static_cast<size_t>(row)], static_cast<int>(left), static_cast<int>(top + 4),
                       static_cast<int>(width), 20, juce::Justification::centredLeft);
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawHorizontalLine(static_cast<int>(centre), left, left + width);

            if (row == 0 || row == 2)
                drawReference(g, left, width, centre, rowHeight * 0.25f, viewStart, viewEnd);
            if (row == 1)
                drawCapture(g, left, width, centre, rowHeight * 0.28f, viewStart, viewEnd, 0.0);
            if (row == 2)
                drawCapture(g, left, width, centre, rowHeight * 0.22f, viewStart, viewEnd,
                            latencyMs / 1000.0);
        }
    }

private:
    static void drawReference(juce::Graphics& g, float left, float width, float centre,
                              float amplitude, double start, double end)
    {
        g.setColour(juce::Colour(0xffffc857));
        for (const auto click : clickTimes)
        {
            const auto x = left + static_cast<float>((click - start) / (end - start)) * width;
            g.drawLine(x, centre - amplitude, x, centre + amplitude, 2.0f);
        }
    }

    void drawCapture(juce::Graphics& g, float left, float width, float centre,
                     float amplitude, double start, double end, double shiftEarlier) const
    {
        float peak = 1.0e-6f;
        for (const auto sample : capture) peak = std::max(peak, std::abs(sample));
        g.setColour(juce::Colour(0xff4bd18b));
        const auto pixels = std::max(1, static_cast<int>(width));
        for (int px = 0; px < pixels; ++px)
        {
            const auto t0 = start + (end - start) * px / pixels + shiftEarlier;
            const auto t1 = start + (end - start) * (px + 1) / pixels + shiftEarlier;
            auto i0 = juce::jlimit<size_t>(0, capture.size(), static_cast<size_t>(std::max(0.0, t0 * sampleRate)));
            auto i1 = juce::jlimit<size_t>(i0, capture.size(), static_cast<size_t>(std::max(0.0, t1 * sampleRate)));
            float localPeak = 0.0f;
            for (auto i = i0; i < i1; ++i) localPeak = std::max(localPeak, std::abs(capture[i]));
            const auto height = amplitude * localPeak / peak;
            g.drawVerticalLine(static_cast<int>(left) + px, centre - height, centre + height);
        }
    }

    std::vector<float> capture;
    double sampleRate = 0.0;
    double latencyMs = 0.0;
};

class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent(juce::String commandLine)
        : autoMode(commandLine.contains("--auto-calibrate"))
    {
        setSize(880, 850);
        title.setText("Simple Recorder - latency proof", juce::dontSendNotification);
        title.setFont(juce::FontOptions(26.0f, juce::Font::bold));
        title.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(title);

        configureCaption(backendCaption, "Audio system");
        configureCaption(inputCaption, "Input device");
        configureCaption(outputCaption, "Output device");

        status.setText("Opening audio devices…", juce::dontSendNotification);
        status.setFont(juce::FontOptions(16.0f));
        status.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(status);

        calibrate.setButtonText("Calibrate speakers to microphone");
        calibrate.onClick = [this] { beginCalibration(); };
        addAndMakeVisible(calibrate);

        for (auto* box : { &backendSelector, &inputSelector, &outputSelector })
        {
            addAndMakeVisible(*box);
            box->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff242735));
        }
        backendSelector.onChange = [this] { populateEndpoints(); };
        inputSelector.onChange = [this] { applyDeviceSetup(); };
        outputSelector.onChange = [this] { applyDeviceSetup(); };

        testSpeakers.setButtonText("Test speakers");
        testSpeakers.onClick = [this] { engine.startSpeakerTest(); };
        addAndMakeVisible(testSpeakers);
        addAndMakeVisible(waveforms);

        engine.onFinished = [this](CalibrationResult r)
        {
            lastResult = r;
            attempt++;
            const bool usable = r.ok;
            juce::String explanation;
            if (usable && r.plausible)
            {
                explanation = "CALIBRATION SUCCESSFUL\n"
                              "Your recording path is delayed by about " + juce::String(r.latencyMs, 1)
                            + " ms. New recordings will be moved earlier by this amount so overdubs line up.\n"
                              "The repeated clicks agreed within " + juce::String(r.scatterMs, 2)
                            + " ms, so this measurement is reliable.";
            }
            else if (usable && !r.plausible)
            {
                explanation = "HIGH LATENCY DETECTED\n"
                              "The microphone recording arrived " + juce::String(r.latencyMs, 1)
                            + " ms after the sound was played. The driver only reports "
                            + juce::String(r.driverLatencyMs, 1) + " ms.\n"
                              "Proposed compensation: move new recordings " + juce::String(r.latencyMs, 1)
                            + " ms earlier. Consider rechecking; otherwise we can try this correction and verify it with an overdub.";
            }
            else
            {
                explanation = "CALIBRATION COULD NOT GET A RELIABLE READING\n"
                              "The clicks did not arrive consistently enough to calculate a safe correction.\n"
                              "Make the speaker test clearly audible, place the microphone closer, and try again.";
            }
            status.setText(explanation, juce::dontSendNotification);
            waveforms.setResult(engine.getCapturedAudio(), engine.getSampleRate(), r.latencyMs);
            calibrate.setEnabled(true);
            calibrate.setButtonText(usable ? "Verify calibration again" : "Try calibration again");
            if (autoMode)
            {
                const auto output = juce::File::getCurrentWorkingDirectory()
                    .getChildFile("calibration-result.json");
                juce::DynamicObject::Ptr object = new juce::DynamicObject();
                object->setProperty("ok", r.ok);
                object->setProperty("plausible", r.plausible);
                object->setProperty("latencyMs", r.latencyMs);
                object->setProperty("driverLatencyMs", r.driverLatencyMs);
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
        populateBackends();
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
        g.fillRoundedRectangle(24.0f, 205.0f, getWidth() - 48.0f, 118.0f, 14.0f);
        g.setColour(juce::Colour(0xff4bd18b));
        g.fillRoundedRectangle(48.0f, 254.0f, (getWidth() - 96.0f) * inputMeter, 18.0f, 7.0f);
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawText("Live input level", 48, 220, getWidth() - 96, 28, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText("Calibration plays seven audible clicks. Keep the microphone still while it runs.",
                   48, 286, getWidth() - 96, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        title.setBounds(32, 24, getWidth() - 64, 38);
        backendCaption.setBounds(32, 66, 210, 22);
        inputCaption.setBounds(252, 66, getWidth() - 284, 22);
        backendSelector.setBounds(32, 88, 210, 36);
        inputSelector.setBounds(252, 88, getWidth() - 284, 36);
        outputCaption.setBounds(32, 130, getWidth() - 64, 22);
        outputSelector.setBounds(32, 152, getWidth() - 250, 36);
        testSpeakers.setBounds(getWidth() - 208, 152, 176, 36);
        calibrate.setBounds(32, 342, getWidth() - 64, 48);
        waveforms.setBounds(32, 406, getWidth() - 64, 258);
        status.setBounds(40, 680, getWidth() - 80, 150);
    }

private:
    void configureCaption(juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
        addAndMakeVisible(label);
    }

    void populateBackends()
    {
        backendSelector.clear();
        int id = 1;
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            backendNames.push_back(type->getTypeName());
            backendSelector.addItem(type->getTypeName(), id++);
        }
        auto preferred = 0;
        for (int i = 0; i < static_cast<int>(backendNames.size()); ++i)
            if (backendNames[static_cast<size_t>(i)].containsIgnoreCase("Windows Audio")) preferred = i;
        if (!backendNames.empty())
            backendSelector.setSelectedItemIndex(preferred, juce::sendNotificationSync);
        else
            status.setText("No audio systems found.", juce::dontSendNotification);
    }

    void populateEndpoints()
    {
        const auto backendIndex = backendSelector.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(backendIndex, static_cast<int>(backendNames.size()))) return;
        const auto backend = backendNames[static_cast<size_t>(backendIndex)];
        deviceManager.setCurrentAudioDeviceType(backend, true);
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        if (type == nullptr) return;
        type->scanForDevices();
        inputNames = type->getDeviceNames(true);
        outputNames = type->getDeviceNames(false);
        inputSelector.clear(juce::dontSendNotification);
        outputSelector.clear(juce::dontSendNotification);
        for (int i = 0; i < inputNames.size(); ++i) inputSelector.addItem(inputNames[i], i + 1);
        for (int i = 0; i < outputNames.size(); ++i) outputSelector.addItem(outputNames[i], i + 1);

        int preferredInput = 0, preferredOutput = 0;
        for (int i = 0; i < inputNames.size(); ++i)
            if (inputNames[i].containsIgnoreCase("Focusrite")) preferredInput = i;
        for (int i = 0; i < outputNames.size(); ++i)
            if (outputNames[i].containsIgnoreCase("Realtek")) preferredOutput = i;
        if (!inputNames.isEmpty()) inputSelector.setSelectedItemIndex(preferredInput, juce::dontSendNotification);
        if (!outputNames.isEmpty()) outputSelector.setSelectedItemIndex(preferredOutput, juce::dontSendNotification);
        applyDeviceSetup();
    }

    void applyDeviceSetup()
    {
        const auto inputIndex = inputSelector.getSelectedItemIndex();
        const auto outputIndex = outputSelector.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(inputIndex, inputNames.size())
            || !juce::isPositiveAndBelow(outputIndex, outputNames.size())) return;
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.inputDeviceName = inputNames[inputIndex];
        setup.outputDeviceName = outputNames[outputIndex];
        setup.sampleRate = 48000.0;
        setup.bufferSize = 256;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.outputChannels.clear();
        setup.inputChannels.setBit(0);
        setup.outputChannels.setRange(0, 2, true);
        const auto error = deviceManager.setAudioDeviceSetup(setup, true);
        auto* device = deviceManager.getCurrentAudioDevice();
        status.setText(error.isNotEmpty()
                           ? error
                           : ("Input: " + setup.inputDeviceName + "\nOutput: " + setup.outputDeviceName
                              + "\nReady: " + (device ? device->getName() : setup.outputDeviceName)),
                       juce::dontSendNotification);
    }

    void selectNamedSetup()
    {
        // Retained for source compatibility with older project state; endpoint
        // selection is now handled independently by applyDeviceSetup().
        applyDeviceSetup();
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

    juce::AudioDeviceManager deviceManager;
    CalibrationEngine engine;
    CalibrationWaveformView waveforms;
    juce::Label title, status, backendCaption, inputCaption, outputCaption;
    juce::TextButton calibrate, testSpeakers;
    juce::ComboBox backendSelector, inputSelector, outputSelector;
    std::vector<juce::String> backendNames;
    juce::StringArray inputNames, outputNames;
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
