// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProjectStore.h"
#include "TrackEffects.h"
#include <cmath>
#include <map>

namespace studio
{
namespace
{
juce::Result readAudio(Clip& clip)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(clip.file));
    if (!reader) return juce::Result::fail("Could not read audio: " + clip.file.getFileName());
    // This first version keeps playback in RAM. Refuse huge files before allocating.
    if (!std::isfinite(reader->sampleRate) || reader->sampleRate <= 0 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::min(48000.0 * 60 * 30, reader->sampleRate * 60 * 30))
        return juce::Result::fail("Please import an audio file shorter than 30 minutes.");
    clip.sampleRate = reader->sampleRate;
    clip.audio = std::make_shared<juce::AudioBuffer<float>>(juce::jmin(2, (int)reader->numChannels),
                                                          (int)reader->lengthInSamples);
    if (!reader->read(clip.audio.get(), 0, clip.audio->getNumSamples(), 0, true, true))
        return juce::Result::fail("Could not load the complete audio file.");
    return juce::Result::ok();
}
bool finitePositive(double x) { return std::isfinite(x) && x > 0; }
}

juce::Result saveProject(const Session& session, const juce::File& file, double playhead)
{
    if (auto r = file.getParentDirectory().createDirectory(); r.failed()) return r;
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 1);
    root->setProperty("name", session.name);
    root->setProperty("bpm", session.bpm);
    root->setProperty("metronome", session.metronome);
    root->setProperty("countInBars", session.countInBars);
    root->setProperty("playhead", playhead);
    juce::Array<juce::var> tracks;
    for (const auto& track : session.tracks)
    {
        auto t = std::make_unique<juce::DynamicObject>();
        t->setProperty("id", track.id); t->setProperty("name", track.name);
        t->setProperty("armed", track.armed); t->setProperty("mute", track.mute);
        t->setProperty("solo", track.solo); t->setProperty("gain", track.gain);
        t->setProperty("input", track.inputChannel);
        t->setProperty("effectPreset", track.effectPresetId);
        t->setProperty("effectsBypassed", track.effectsBypassed);
        t->setProperty("reverbEnabled",track.reverb.enabled);
        t->setProperty("reverbStyle",track.reverb.style);
        t->setProperty("reverbMix",track.reverb.mix);
        juce::Array<juce::var> clips;
        for (const auto& clip : track.clips)
        {
            auto c = std::make_unique<juce::DynamicObject>();
            c->setProperty("id", clip.id);
            c->setProperty("file", clip.file.getRelativePathFrom(file.getParentDirectory()));
            c->setProperty("start", clip.startSeconds);
            c->setProperty("offset", clip.sourceOffsetSeconds);
            c->setProperty("length", clip.lengthSeconds);
            c->setProperty("gain", clip.gain);
            clips.add(juce::var(c.release()));
        }
        t->setProperty("clips", clips);
        tracks.add(juce::var(t.release()));
    }
    root->setProperty("tracks", tracks);
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(root.release()), false))
        || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Project could not be saved. Audio takes remain in the Media folder.");
    return juce::Result::ok();
}

juce::Result loadProject(Session& session, const juce::File& file, double& playhead)
{
    auto json = juce::JSON::parse(file);
    if (!json.isObject() || (int)json["version"] != 1 || !json["tracks"].isArray())
        return juce::Result::fail("This is not a supported Simple Recorder project.");
    Session next;
    next.name = json["name"].toString();
    next.bpm = juce::jlimit(40.0, 240.0, (double)json["bpm"]);
    next.metronome = (bool)json["metronome"];
    next.countInBars = juce::jlimit(0, 1, (int)json["countInBars"]);
    std::map<juce::String, Clip> loaded;
    for (const auto& t : *json["tracks"].getArray())
    {
        if (!t.isObject() || !t["clips"].isArray()) return juce::Result::fail("Invalid track in project.");
        Track track;
        track.id = t["id"].toString(); track.name = t["name"].toString();
        track.armed = (bool)t["armed"]; track.mute = (bool)t["mute"]; track.solo = (bool)t["solo"];
        track.gain = (float)juce::jlimit(0.0, 4.0, (double)t["gain"]);
        track.inputChannel = juce::jlimit(0, 63, (int)t["input"]);
        track.effectPresetId = effectPreset(t["effectPreset"].toString()).id;
        track.effectsBypassed = (bool)t["effectsBypassed"];
        track.reverb.enabled=(bool)t["reverbEnabled"];
        track.reverb.style=validReverbStyle(t["reverbStyle"].toString());
        const double mix=t.hasProperty("reverbMix")?(double)t["reverbMix"]:0.2;
        track.reverb.mix=std::isfinite(mix)?(float)juce::jlimit(0.0,1.0,mix):0.2f;
        for (const auto& c : *t["clips"].getArray())
        {
            Clip clip;
            clip.id = c["id"].toString();
            clip.file = file.getParentDirectory().getChildFile(c["file"].toString());
            clip.startSeconds = (double)c["start"]; clip.sourceOffsetSeconds = (double)c["offset"];
            clip.lengthSeconds = (double)c["length"]; clip.gain = (double)c["gain"];
            if (!std::isfinite(clip.startSeconds) || clip.startSeconds < 0
                || !std::isfinite(clip.sourceOffsetSeconds) || clip.sourceOffsetSeconds < 0
                || !finitePositive(clip.lengthSeconds) || !std::isfinite(clip.gain) || clip.gain < 0)
                return juce::Result::fail("Invalid clip timing or level in project.");
            const auto key = clip.file.getFullPathName();
            if (!loaded.contains(key))
            {
                if (auto r = readAudio(clip); r.failed()) return r;
                loaded[key] = clip;
            }
            else { clip.audio = loaded[key].audio; clip.sampleRate = loaded[key].sampleRate; }
            if (clip.sourceOffsetSeconds + clip.lengthSeconds > clip.audio->getNumSamples() / clip.sampleRate + 0.002)
                return juce::Result::fail("Audio is shorter than the clip saved in the project.");
            track.clips.push_back(std::move(clip));
        }
        next.tracks.push_back(std::move(track));
    }
    playhead = (double)json["playhead"];
    if (!std::isfinite(playhead) || playhead < 0) playhead = 0;
    session = std::move(next);
    return juce::Result::ok();
}

juce::Result importAudio(Clip& clip, const juce::File& source, const juce::File& mediaDirectory)
{
    clip.file = source;
    if (auto r = readAudio(clip); r.failed()) return r;
    if (auto r = mediaDirectory.createDirectory(); r.failed()) return r;
    auto destination = mediaDirectory.getNonexistentChildFile(source.getFileNameWithoutExtension(), source.getFileExtension());
    if (!source.copyFileTo(destination)) return juce::Result::fail("Could not copy imported audio into the project.");
    clip.file = destination; clip.id = juce::Uuid().toString();
    clip.lengthSeconds = clip.audio->getNumSamples() / clip.sampleRate;
    return juce::Result::ok();
}

juce::Result exportMix(const Session& session, const juce::File& destination, double sampleRate)
{
    double end = 0;
    bool anySolo = false;
    for (const auto& t : session.tracks) { anySolo |= t.solo; for (const auto& c : t.clips) end = std::max(end, c.startSeconds + c.lengthSeconds); }
    if (end <= 0) return juce::Result::fail("Record or import something before exporting.");
    const double dryEnd=end;
    for(const auto& t:session.tracks)
        if(!t.mute && (!anySolo || t.solo))
            for(const auto& c:t.clips)end=std::max(end,c.startSeconds+c.lengthSeconds+reverbTailSeconds(t.reverb));
    juce::TemporaryFile temporary(destination);
    std::unique_ptr<juce::FileOutputStream> stream(temporary.getFile().createOutputStream());
    if (!stream || !stream->openedOk()) return juce::Result::fail("Could not create the export file.");
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions().withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(24);
    std::unique_ptr<juce::OutputStream> output(std::move(stream));
    auto writer = wav.createWriterFor(output, options);
    if (!writer) return juce::Result::fail("Could not start WAV export.");
    juce::AudioBuffer<float> block(2, 1024);
    juce::AudioBuffer<float> trackBlock(2,1024);
    std::vector<TrackEffects> effects(session.tracks.size());
    for(size_t i=0;i<effects.size();++i)
        effects[i].prepare(session.tracks[i].effectPresetId,session.tracks[i].effectsBypassed,sampleRate,session.tracks[i].reverb);
    const auto count = (juce::int64)std::ceil(end * sampleRate);
    for (juce::int64 pos = 0; pos < count; pos += block.getNumSamples())
    {
        block.clear();
        const int n = (int)std::min<juce::int64>(block.getNumSamples(), count - pos);
        for (size_t t=0;t<session.tracks.size();++t)
        {
            const auto& track=session.tracks[t];
            if (track.mute || (anySolo && !track.solo)) continue;
            renderTrackAudio(track,pos/sampleRate,sampleRate,trackBlock.getWritePointer(0),trackBlock.getWritePointer(1),n);
            effects[t].process(trackBlock.getWritePointer(0),trackBlock.getWritePointer(1),n);
            for(int ch=0;ch<2;++ch) block.addFrom(ch,0,trackBlock,ch,0,n,track.gain);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float fade=end>dryEnd?(float)juce::jlimit(0.0,1.0,(end-(pos+i)/sampleRate)/0.1):1.0f;
                block.setSample(ch,i,juce::jlimit(-1.0f,1.0f,block.getSample(ch,i)*fade));
            }
        if (!writer->writeFromAudioSampleBuffer(block, 0, n)) return juce::Result::fail("The disk could not finish the export.");
    }
    writer.reset();
    if (!temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail("Could not finish the export file.");
    return juce::Result::ok();
}
}
