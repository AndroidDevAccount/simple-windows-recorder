// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/SessionEngine.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace studio
{
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool close(double a, double b, double epsilon = 1.0e-8) { return std::abs(a - b) <= epsilon; }

Clip constantClip(double start, double length, float value, double rate = 48000.0)
{
    Clip clip;
    clip.id = juce::Uuid().toString();
    clip.startSeconds = start;
    clip.lengthSeconds = length;
    clip.sampleRate = rate;
    clip.audio = std::make_shared<juce::AudioBuffer<float>>(1, static_cast<int>(std::llround(length * rate)));
    auto* samples = clip.audio->getWritePointer(0);
    std::fill(samples, samples + clip.audio->getNumSamples(), value);
    return clip;
}

struct FakeDevice
{
    static constexpr int block = 256;
    static constexpr double rate = 48000.0;
    SessionEngine& engine;
    juce::AudioBuffer<float> input { 2, block }, output { 2, block };
    std::int64_t inputSample = 0;
    int blocks = 0;
    std::function<float(int, std::int64_t)> signal = [] (int, std::int64_t) { return 0.0f; };

    void tick(int count = block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                input.setSample(channel, i, signal(channel, inputSample + i));
        engine.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 2,
            output.getArrayOfWritePointers(), 2, count, {});
        inputSample += count;
        // Simulated audio must occasionally give the real disk worker time to
        // consume its ring; hardware normally provides 5.3 ms between callbacks.
        if (++blocks % 8 == 0)
            juce::Thread::sleep(1);
    }

    void advance(std::int64_t samples)
    {
        while (samples > 0)
        {
            const auto count = static_cast<int>(std::min<std::int64_t>(block, samples));
            tick(count);
            samples -= count;
        }
    }

    bool finish()
    {
        bool changed = false;
        for (int i = 0; i < 3000 && engine.isBusy(); ++i)
        {
            tick();
            changed = engine.poll() || changed;
            juce::Thread::sleep(1);
        }
        require(!engine.isBusy(), "Recording never finalized");
        return changed;
    }
};

void addTrack(SessionEngine& engine, const juce::String& name, int input, float oldValue)
{
    Track track;
    track.id = juce::Uuid().toString();
    track.name = name;
    track.armed = true;
    track.inputChannel = input;
    track.clips.push_back(constantClip(0, 60, oldValue));
    engine.session().tracks.push_back(std::move(track));
}

const Clip& findPunch(const Track& track, double start)
{
    const auto found = std::find_if(track.clips.begin(), track.clips.end(),
        [start] (const Clip& clip) { return close(clip.startSeconds, start); });
    require(found != track.clips.end(), "Punch was not placed at the requested playhead");
    return *found;
}
}

bool runEngineTests(juce::String& report)
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("SimpleRecorder-engine-tests-" + juce::Uuid().toString());
    report.clear();
    int passed = 0;
    try
    {
        require(directory.createDirectory().wasOk(), "Could not create test directory");
        {
            Track track;
            auto original = constantClip(0, 60, 0.15f);
            original.sourceOffsetSeconds = 3.0;
            track.clips.push_back(original);
            auto punch = constantClip(30, 2, 0.3f);
            SessionEngine::insertPunch(track, punch);
            require(track.clips.size() == 3, "Punch must retain both original sides");
            require(close(track.clips[0].startSeconds, 0) && close(track.clips[0].lengthSeconds, 30), "Incorrect left punch boundary");
            require(close(track.clips[1].startSeconds, 30) && close(track.clips[1].lengthSeconds, 2), "Punch appended instead of replacing at 30 seconds");
            require(close(track.clips[2].startSeconds, 32) && close(track.clips[2].lengthSeconds, 28), "Incorrect right punch boundary");
            require(close(track.clips[2].sourceOffsetSeconds, 35), "Right fragment lost the original source offset");
            require(track.clips[0].audio == original.audio && track.clips[2].audio == original.audio, "Original audio was destructively changed");
            ++passed;
            report += "PASS: punch at 30 seconds preserves both old sides and source offsets.\n";
        }
        {
            SessionEngine engine;
            engine.prepareForDevice(FakeDevice::rate, FakeDevice::block, 2);
            engine.session().countInBars = 0;
            addTrack(engine, "Voice", 0, 0.15f);
            addTrack(engine, "Guitar", 1, -0.1f);
            constexpr std::int64_t latency = 1622;
            constexpr std::int64_t duration = 96000;
            constexpr std::int64_t marker = 24000;
            FakeDevice device { engine };
            device.signal = [] (int channel, std::int64_t inputFrame)
            {
                const auto frame = inputFrame - latency;
                if (frame < 0 || frame >= duration)
                    return 0.0f;
                if (frame == marker || frame == duration - 1)
                    return channel == 0 ? 0.7f : -0.6f;
                return channel == 0 ? 0.3f : -0.2f;
            };
            require(engine.record(30.0, latency * 1000.0 / FakeDevice::rate, directory), "Could not begin multitrack recording");
            device.advance(duration);
            engine.stop();
            require(device.finish(), "Completed punch did not update session");
            require(engine.lastError().isEmpty(), "Unexpected recording error");
            require(engine.session().tracks[0].clips.size() == 3 && engine.session().tracks[1].clips.size() == 3, "Both armed tracks must be punched");
            for (int track = 0; track < 2; ++track)
            {
                const auto& clip = findPunch(engine.session().tracks[static_cast<std::size_t>(track)], 30.0);
                require(close(clip.lengthSeconds, 2.0), "Latency changed the punch duration");
                require(close(clip.sourceOffsetSeconds * FakeDevice::rate, latency), "Wrong compensation trim");
                require(clip.audio->getNumSamples() == duration + latency, "Stop failed to retain the compensation tail");
                require(close(clip.audio->getSample(0, static_cast<int>(latency + marker)), track == 0 ? 0.7 : -0.6, 1.0e-6), "Input channels or compensated marker were misaligned");
                require(close(clip.audio->getSample(0, static_cast<int>(latency + duration - 1)), track == 0 ? 0.7 : -0.6, 1.0e-6), "The final performance sample was lost");
                require(clip.file.existsAsFile(), "Raw take was not written to disk");
            }
            engine.session().tracks[0].solo = true;
            require(engine.play(31.0), "Could not play recorded punch");
            device.tick();
            require(close(device.output.getSample(0, 100), 0.3, 1.0e-6), "Old and new takes overlapped in the punch, or solo failed");
            engine.stop();
            device.tick();
            require(engine.undo(), "Recording undo unavailable");
            require(engine.session().tracks[0].clips.size() == 1 && engine.session().tracks[1].clips.size() == 1, "One Undo must restore every armed track");
            ++passed;
            report += "PASS: two-input 30-32 second punch, 1622-sample compensation, final tail, playback, solo, disk WAV, and atomic Undo.\n";
        }
        {
            SessionEngine engine;
            engine.prepareForDevice(FakeDevice::rate, FakeDevice::block, 2);
            engine.session().countInBars = 0;
            addTrack(engine, "At zero", 0, 0.15f);
            constexpr std::int64_t latency = 1622;
            FakeDevice device { engine };
            device.signal = [] (int, std::int64_t frame) { return frame == latency ? 0.8f : 0.0f; };
            require(engine.record(0.0, latency * 1000.0 / FakeDevice::rate, directory), "Could not record at zero");
            device.advance(4800);
            engine.stop();
            require(device.finish(), "Zero-start take missing");
            const auto& clip = findPunch(engine.session().tracks[0], 0.0);
            require(close(clip.startSeconds, 0.0), "Zero-start compensation moved the take before zero");
            require(close(clip.audio->getSample(0, static_cast<int>(latency)), 0.8, 1.0e-6), "First performance sample was discarded at zero");
            require(close(clip.lengthSeconds, 0.1), "Zero-start punch duration changed");
            ++passed;
            report += "PASS: compensation at song start preserves the first performance sample.\n";
        }
        {
            SessionEngine engine;
            engine.prepareForDevice(FakeDevice::rate, FakeDevice::block, 2);
            engine.session().countInBars = 1;
            engine.session().bpm = 120;
            addTrack(engine, "Count-in", 0, 0.15f);
            FakeDevice device { engine };
            require(engine.record(30.0, 33.8, directory), "Could not start count-in");
            device.advance(1000);
            require(engine.isCountingIn() && close(engine.position(), 30.0), "Count-in moved the punch anchor");
            engine.stop();
            require(!device.finish(), "Canceling count-in must not add a take");
            require(engine.session().tracks[0].clips.size() == 1, "Canceling count-in changed existing audio");
            ++passed;
            report += "PASS: count-in holds the playhead; cancel leaves earlier audio unchanged.\n";
        }
        {
            SessionEngine engine;
            engine.prepareForDevice(FakeDevice::rate, FakeDevice::block, 2);
            engine.session().countInBars = 1;
            engine.session().bpm = 120;
            addTrack(engine, "After count-in", 0, 0.15f);
            FakeDevice device { engine };
            constexpr std::int64_t countInSamples = 96000;
            constexpr std::int64_t latency = 1622;
            device.signal = [] (int, std::int64_t frame) { return frame == countInSamples + latency + 1000 ? 0.8f : 0.0f; };
            require(engine.record(30.0, latency * 1000.0 / FakeDevice::rate, directory), "Could not start count-in capture");
            device.advance(countInSamples);
            require(!engine.isCountingIn() && close(engine.position(), 30.0), "Count-in boundary advanced the song");
            device.advance(4800);
            engine.stop();
            require(device.finish(), "Count-in capture missing");
            const auto& clip = findPunch(engine.session().tracks[0], 30.0);
            require(close(clip.lengthSeconds, 0.1), "Count-in leaked into take duration");
            require(close(clip.audio->getSample(0, static_cast<int>(latency + 1000)), 0.8, 1.0e-6), "Count-in leaked into take source offset");
            ++passed;
            report += "PASS: complete count-in enters capture on the exact anchor without offsetting the take.\n";
        }
        {
            SessionEngine engine;
            engine.prepareForDevice(FakeDevice::rate, FakeDevice::block, 2);
            engine.session().countInBars = 0;
            addTrack(engine, "Device loss", 0, 0.15f);
            FakeDevice device { engine };
            device.signal = [] (int, std::int64_t) { return 0.2f; };
            require(engine.record(30.0, 33.8, directory), "Could not start device-loss test");
            device.advance(4800);
            engine.audioDeviceStopped();
            bool changed = false;
            for (int i = 0; i < 3000 && engine.isBusy(); ++i)
            {
                changed = engine.poll() || changed;
                juce::Thread::sleep(1);
            }
            require(!engine.isBusy() && changed, "Device loss failed to preserve completed capture");
            require(engine.lastError().isNotEmpty(), "Device loss was not reported");
            require(findPunch(engine.session().tracks[0], 30.0).lengthSeconds < 0.1, "Unavailable device-loss tail was incorrectly claimed as recorded");
            ++passed;
            report += "PASS: device loss drains disk data and trims the unavailable tail.\n";
        }
        report += juce::String(passed) + " engine regression groups passed.\n";
        directory.deleteRecursively();
        return true;
    }
    catch (const std::exception& error)
    {
        report += "FAIL after " + juce::String(passed) + " groups: " + error.what() + "\n";
        report += "Test artifacts retained: " + directory.getFullPathName() + "\n";
        return false;
    }
}
}
