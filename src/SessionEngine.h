// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "TrackReverb.h"
#include "InputAnalysis.h"

namespace studio
{
struct Clip
{
    juce::String id;
    juce::File file;
    double startSeconds = 0.0;
    double sourceOffsetSeconds = 0.0;
    double lengthSeconds = 0.0;
    double gain = 1.0;
    double sampleRate = 48000.0;
    std::shared_ptr<juce::AudioBuffer<float>> audio;
};

struct Track
{
    juce::String id, name;
    bool armed = false, mute = false, solo = false;
    float gain = 1.0f;
    juce::String effectPresetId { "dry" };
    bool effectsBypassed = true;
    bool peakTamerEnabled = false;
    ReverbSettings reverb;
    // Zero-based physical device input channel, not callback-array index.
    int inputChannel = -1; // -1 follows saved global default, >=0 is explicit physical channel.
    std::vector<Clip> clips;
};

struct Session
{
    juce::String name { "Untitled song" };
    double bpm = 100.0;
    bool metronome = false;
    int countInBars = 1;
    float masterGain = 1.0f;
    std::vector<Track> tracks;
};

// Changes to session() are UI-thread-only, while !isBusy(). Audio playback
// holds its own immutable snapshot. Audio buffers referenced by clips must
// likewise not be edited in place while playing.
class SessionEngine final : public juce::AudioIODeviceCallback
{
public:
    SessionEngine();
    ~SessionEngine() override;

    Session& session() noexcept;
    const Session& session() const noexcept;
    bool play(double startSeconds);
    bool record(double startSeconds, double compensationMs, const juce::File& takesDirectory);
    // Recording stops on the next callback boundary. Capture continues silently
    // for compensationMs to retain the final played note. poll() commits the take.
    void stop() noexcept;
    void seek(double seconds) noexcept;
    bool isPlaying() const noexcept;
    bool isRecording() const noexcept;
    bool isBusy() const noexcept;
    bool isCountingIn() const noexcept;
    int countInBeatsRemaining() const noexcept;
    double position() const noexcept;
    double recordingStart() const noexcept;
    double recordingEnd() const noexcept;
    double duration() const noexcept;
    double sampleRate() const noexcept;
    float inputPeak(int physicalChannel) const noexcept;
    float outputPeak() const noexcept;
    int inputChannelCount() const noexcept;
    bool readLivePeak(LivePeak&);
    void setTunerInput(int channel);
    bool pollPitch(PitchResult&);
    void setDefaultInput(int);
    // Call on the UI timer. True means a recording was committed to session().
    // Even on failure, this releases the transport when finalization completes.
    bool poll();
    juce::String lastError() const;
    void clearError();
    bool canUndo() const noexcept;
    bool undo();

    // Shared deterministic edit operation, also used by engine regression tests.
    static void insertPunch(Track&, const Clip& replacement);

    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String&) override;
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int,
                                         int, const juce::AudioIODeviceCallbackContext&) override;
    // Test/device-adapter entry point. Normally JUCE calls audioDeviceAboutToStart.
    void prepareForDevice(double sampleRate, int blockSize, int numInputs);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

bool runEngineTests(juce::String& report);
}
