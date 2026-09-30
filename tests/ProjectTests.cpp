// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/ProjectStore.h"
#include <cmath>

namespace studio
{
namespace
{
struct TestDirectory
{
    juce::File parent { juce::File::getSpecialLocation(juce::File::tempDirectory) };
    juce::File directory { parent.getChildFile("simple-recorder-project-tests-" + juce::Uuid().toString()) };
    ~TestDirectory()
    {
        // Only remove the uniquely created fixture folder, never project/user media.
        if (directory.getParentDirectory() == parent
            && directory.getFileName().startsWith("simple-recorder-project-tests-"))
            directory.deleteRecursively();
    }
};

bool writeFixture(const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (!stream) return false;
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(48000.0).withNumChannels(audio.getNumChannels()).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

bool near(double actual, double expected, double tolerance = 1.0e-8)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}
}

bool runProjectTests(juce::String& report)
{
    const auto fail = [&report] (const juce::String& message)
    {
        report += "FAIL project: " + message + "\n";
        return false;
    };
    TestDirectory fixture;
    const auto media = fixture.directory.getChildFile("Media");
    if (media.createDirectory().failed()) return fail("could not create temporary fixture folder");

    constexpr int rate = 48000, delay = 1622;
    auto oldAudio = std::make_shared<juce::AudioBuffer<float>>(1, 4 * rate);
    juce::FloatVectorOperations::fill(oldAudio->getWritePointer(0), 0.2f, oldAudio->getNumSamples());
    auto takeAudio = std::make_shared<juce::AudioBuffer<float>>(1, 2 * rate + delay);
    juce::FloatVectorOperations::fill(takeAudio->getWritePointer(0), 0.9f, delay);
    juce::FloatVectorOperations::fill(takeAudio->getWritePointer(0) + delay, 0.6f, 2 * rate);
    takeAudio->setSample(0, delay + rate, 0.8f);

    Clip original;
    original.id = "original"; original.file = media.getChildFile("original.wav");
    original.startSeconds = 29.0; original.lengthSeconds = 4.0;
    original.gain = 0.75; original.sampleRate = rate; original.audio = oldAudio;
    Clip replacement;
    replacement.id = "punch"; replacement.file = media.getChildFile("punch.wav");
    replacement.startSeconds = 30.0; replacement.lengthSeconds = 2.0;
    replacement.sourceOffsetSeconds = static_cast<double>(delay) / rate;
    replacement.gain = 0.4; replacement.sampleRate = rate; replacement.audio = takeAudio;
    if (!writeFixture(original.file, *oldAudio) || !writeFixture(replacement.file, *takeAudio))
        return fail("could not write floating-point source WAVs");

    Session source;
    source.name = "Punch round trip"; source.bpm = 113; source.metronome = true; source.countInBars = 0;source.masterGain=0.5f;
    Track guitar;
    guitar.id = "guitar"; guitar.name = "Guitar input 2"; guitar.inputChannel = 1;
    guitar.armed = true; guitar.solo = true; guitar.gain = 0.5f;
    guitar.clips.push_back(original);
    SessionEngine::insertPunch(guitar, replacement);
    source.tracks.push_back(guitar);
    Track excluded;
    excluded.id = "excluded"; excluded.name = "Excluded by solo"; excluded.gain = 1.0f;
    excluded.clips.push_back(original);
    source.tracks.push_back(excluded);
    excluded.id = "muted"; excluded.name = "Muted despite solo"; excluded.solo = true; excluded.mute = true;
    source.tracks.push_back(excluded);

    const auto project = fixture.directory.getChildFile("song.swr");
    if (auto result = saveProject(source, project, 30.25); result.failed())
        return fail(result.getErrorMessage());
    const auto manifest = juce::JSON::parse(project);
    if (juce::File::isAbsolutePath(manifest["tracks"][0]["clips"][0]["file"].toString()))
        return fail("saved media path is absolute, so the project cannot be moved together with its Media folder");

    Session restored;
    double playhead = 0.0;
    if (auto result = loadProject(restored, project, playhead); result.failed())
        return fail(result.getErrorMessage());
    if (restored.name != source.name || !near(restored.bpm, 113.0) || !restored.metronome
        || restored.countInBars != 0 || !near(restored.masterGain,0.5) || !near(playhead, 30.25) || restored.tracks.size() != 3)
        return fail("session name, tempo, metronome, disabled count-in, playhead or tracks changed after reload");
    const auto& restoredGuitar = restored.tracks[0];
    if (restoredGuitar.id != guitar.id || restoredGuitar.name != guitar.name || !restoredGuitar.armed
        || !restoredGuitar.solo || restoredGuitar.mute || restoredGuitar.inputChannel != 1
        || !near(restoredGuitar.gain, 0.5) || restoredGuitar.clips.size() != 3)
        return fail("track identity, arm, input, mute/solo, gain or punch fragments changed after reload");
    const auto& left = restoredGuitar.clips[0];
    const auto& punch = restoredGuitar.clips[1];
    const auto& right = restoredGuitar.clips[2];
    if (!near(left.startSeconds, 29.0) || !near(left.lengthSeconds, 1.0) || !near(left.sourceOffsetSeconds, 0.0)
        || !near(punch.startSeconds, 30.0) || !near(punch.lengthSeconds, 2.0)
        || !near(punch.sourceOffsetSeconds, static_cast<double>(delay) / rate)
        || !near(punch.gain, 0.4) || !near(right.startSeconds, 32.0)
        || !near(right.lengthSeconds, 1.0) || !near(right.sourceOffsetSeconds, 3.0))
        return fail("punch boundaries or latency-compensated source offsets changed on disk round trip");
    if (left.audio != right.audio || left.file != original.file || punch.file != replacement.file
        || !near(punch.audio->getSample(0, delay + rate), 0.8, 1.0e-6))
        return fail("source WAV data or shared split-clip media changed after reload");
    report += "PASS project: punched clips, source offsets, input/arm/mute/solo/gain and count-in survive disk round trip\n";

    source.countInBars = 1;
    if (auto result = saveProject(source, project, 31.0); result.failed()) return fail(result.getErrorMessage());
    if (auto result = loadProject(restored, project, playhead); result.failed()) return fail(result.getErrorMessage());
    if (restored.countInBars != 1 || !near(playhead, 31.0))
        return fail("atomic overwrite did not restore the updated count-in and playhead");

    const auto mixFile = fixture.directory.getChildFile("mix.wav");
    if (auto result = exportMix(restored, mixFile, rate); result.failed()) return fail(result.getErrorMessage());
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(mixFile.createInputStream().release(), true));
    if (!reader || reader->numChannels != 2 || reader->lengthInSamples != 33LL * rate
        || !near(reader->sampleRate, rate) || reader->bitsPerSample != 24)
        return fail("export WAV must be stereo, 24-bit, 48 kHz and exactly 33 seconds long");
    juce::AudioBuffer<float> rendered(2, static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&rendered, 0, rendered.getNumSamples(), 0, true, true)) return fail("could not read exported mix");
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto sample = [&] (double seconds) { return rendered.getSample(channel, (int)std::llround(seconds * rate)); };
        if (!near(sample(10.0), 0.0, 1.0e-6) || !near(sample(29.5), 0.0375, 2.0e-6)
            || !near(sample(30.5), 0.06, 2.0e-6) || !near(sample(31.0), 0.08, 2.0e-6)
            || !near(sample(32.5), 0.0375, 2.0e-6))
            return fail("export does not preserve outside audio, corrected marker, punch replacement, gain or mute/solo");
    }
    reader.reset();
    report += "PASS project: exported stereo WAV preserves old audio around the punch and the compensated marker at 31 s\n";

    auto mixLevel=analyseMixLevel(restored,rate);
    if(!mixLevel.valid||mixLevel.samplePeakDb>-20||mixLevel.rating!="MIX IS QUIET"||mixLevel.suggestedMasterDb<=0)
        return fail("mix check did not identify the deliberately quiet processed fixture");
    Clip quiet;quiet.sampleRate=rate;quiet.lengthSeconds=1;quiet.audio=std::make_shared<juce::AudioBuffer<float>>(1,rate);juce::FloatVectorOperations::fill(quiet.audio->getWritePointer(0),0.1f,rate);
    const auto inputLevel=analyseRecordingLevel(quiet);
    if(!inputLevel.valid||!near(inputLevel.peakDb,-20.0,0.05)||inputLevel.rating!="SAFE, SLIGHTLY QUIET")return fail("input health guidance misclassified a -20 dBFS take");
    report += "PASS project: saved Master affects export; input health and processed LUFS/peak mix guidance are deterministic\n";

    auto broken = source;
    broken.tracks[0].clips[0].file = media.getChildFile("missing.wav");
    const auto brokenFile = fixture.directory.getChildFile("missing-media.swr");
    if (auto result = saveProject(broken, brokenFile, 0.0); result.failed()) return fail(result.getErrorMessage());
    restored.name = "Keep this open project";
    const auto missing = loadProject(restored, brokenFile, playhead);
    if (missing.wasOk() || restored.name != "Keep this open project" || !near(playhead, 31.0))
        return fail("failed project load modified the current session");
    report += "PASS project: missing-media load fails without replacing the open session\n";
    return true;
}
}
