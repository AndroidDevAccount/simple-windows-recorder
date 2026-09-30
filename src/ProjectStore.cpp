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
    root->setProperty("masterGain",session.masterGain);
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
    {const double gain=json.hasProperty("masterGain")?(double)json["masterGain"]:1.0;next.masterGain=std::isfinite(gain)?(float)juce::jlimit(0.0,4.0,gain):1.0f;}
    std::map<juce::String, Clip> loaded;
    for (const auto& t : *json["tracks"].getArray())
    {
        if (!t.isObject() || !t["clips"].isArray()) return juce::Result::fail("Invalid track in project.");
        Track track;
        track.id = t["id"].toString(); track.name = t["name"].toString();
        track.armed = (bool)t["armed"]; track.mute = (bool)t["mute"]; track.solo = (bool)t["solo"];
        track.gain = (float)juce::jlimit(0.0, 4.0, (double)t["gain"]);
        track.inputChannel = juce::jlimit(-1, 63, t.hasProperty("input")?(int)t["input"]:-1);
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
                block.setSample(ch,i,juce::jlimit(-1.0f,1.0f,block.getSample(ch,i)*session.masterGain*fade));
            }
        if (!writer->writeFromAudioSampleBuffer(block, 0, n)) return juce::Result::fail("The disk could not finish the export.");
    }
    writer.reset();
    if (!temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail("Could not finish the export file.");
    return juce::Result::ok();
}

RecordingLevelAnalysis analyseRecordingLevel(const Clip& clip)
{
    RecordingLevelAnalysis result;
    if(!clip.audio||clip.sampleRate<=0||clip.lengthSeconds<=0)return result;
    const int first=juce::jlimit(0,clip.audio->getNumSamples(),(int)std::llround(clip.sourceOffsetSeconds*clip.sampleRate));
    const int count=juce::jlimit(0,clip.audio->getNumSamples()-first,(int)std::llround(clip.lengthSeconds*clip.sampleRate));
    if(count<=0)return result;
    float peak=0;std::vector<double> windows;const int window=std::max(1,(int)std::llround(clip.sampleRate*0.05));
    for(int pos=0;pos<count;pos+=window)
    {const int n=std::min(window,count-pos);double energy=0;
    for(int ch=0;ch<clip.audio->getNumChannels();++ch){const auto* data=clip.audio->getReadPointer(ch,first+pos);for(int i=0;i<n;++i){peak=std::max(peak,std::abs(data[i]));energy+=(double)data[i]*data[i];}}
    windows.push_back(energy/(n*clip.audio->getNumChannels()));}
    result.valid=true;result.peakDb=juce::Decibels::gainToDecibels(peak,-100.0f);
    const double gate=std::pow(10.0,std::max(-55.0,result.peakDb-35.0)/10.0);double active=0;int activeCount=0;
    for(const auto energy:windows)if(energy>=gate){active+=energy;++activeCount;}
    result.activeRmsDb=activeCount?10*std::log10(active/activeCount):-100;
    if(result.peakDb>=-0.5){result.rating="CLIPPING RISK";result.advice="Turn down the physical input gain on the interface and record again; the raw take is at the digital ceiling.";}
    else if(result.peakDb>-3){result.rating="HOT";result.advice="The input is very hot. Lower the interface gain a little to leave room for unexpected peaks.";}
    else if(result.peakDb>=-18){result.rating="HEALTHY INPUT";result.advice="The untouched recording has useful level and safe headroom. Set its place in the song with track Gain, not the interface knob.";}
    else if(result.peakDb>=-24){result.rating="SAFE, SLIGHTLY QUIET";result.advice="This is clean and usable. You may raise the interface gain on the next take if hiss is noticeable, but software Gain is fine for mix balance.";}
    else {result.rating="QUIET INPUT";result.advice="Raise the physical input gain on the interface for the next take. Software Gain can make this louder, but it also raises recorded noise.";}
    return result;
}

MixLevelAnalysis analyseMixLevel(const Session& session,double sampleRate)
{
    MixLevelAnalysis result;double end=0,dryEnd=0;bool anySolo=false;
    for(const auto& track:session.tracks){anySolo|=track.solo;for(const auto& clip:track.clips)dryEnd=end=std::max(end,clip.startSeconds+clip.lengthSeconds);}
    if(end<=0||sampleRate<=0)return result;
    for(const auto& track:session.tracks)if(!track.mute&&(!anySolo||track.solo))for(const auto& clip:track.clips)end=std::max(end,clip.startSeconds+clip.lengthSeconds+reverbTailSeconds(track.reverb));
    juce::AudioBuffer<float> mix(2,1024),trackAudio(2,1024);std::vector<TrackEffects> effects(session.tracks.size());
    for(size_t i=0;i<effects.size();++i)effects[i].prepare(session.tracks[i].effectPresetId,session.tracks[i].effectsBypassed,sampleRate,session.tracks[i].reverb);
    using Coeff=juce::dsp::IIR::Coefficients<float>;juce::dsp::IIR::Filter<float> hpL,hpR,shelfL,shelfR;
    hpL.coefficients=hpR.coefficients=Coeff::makeHighPass(sampleRate,38.135470876f,0.5003270373f);shelfL.coefficients=shelfR.coefficients=Coeff::makeHighShelf(sampleRate,1681.974451f,0.707175237f,juce::Decibels::decibelsToGain(4.0f));
    const int loudnessWindow=std::max(1,(int)std::llround(sampleRate*0.4)),hop=std::max(1,(int)std::llround(sampleRate*0.1));
    std::vector<double> ring((size_t)loudnessWindow),blocks;int ringPos=0,ringCount=0,hopCount=0;double rolling=0;float peak=0;
    const auto total=(juce::int64)std::ceil(end*sampleRate);
    for(juce::int64 pos=0;pos<total;pos+=1024)
    {mix.clear();const int n=(int)std::min<juce::int64>(1024,total-pos);
    for(size_t t=0;t<session.tracks.size();++t){const auto& track=session.tracks[t];if(track.mute||(anySolo&&!track.solo))continue;renderTrackAudio(track,pos/sampleRate,sampleRate,trackAudio.getWritePointer(0),trackAudio.getWritePointer(1),n);effects[t].process(trackAudio.getWritePointer(0),trackAudio.getWritePointer(1),n);for(int ch=0;ch<2;++ch)mix.addFrom(ch,0,trackAudio,ch,0,n,track.gain);}
    for(int i=0;i<n;++i){const float fade=end>dryEnd?(float)juce::jlimit(0.0,1.0,(end-(pos+i)/sampleRate)/0.1):1.0f;const float left=mix.getSample(0,i)*session.masterGain*fade,right=mix.getSample(1,i)*session.masterGain*fade;peak=std::max({peak,std::abs(left),std::abs(right)});const float kl=shelfL.processSample(hpL.processSample(left)),kr=shelfR.processSample(hpR.processSample(right));const double energy=(double)kl*kl+(double)kr*kr;if(ringCount==loudnessWindow)rolling-=ring[(size_t)ringPos];else ++ringCount;ring[(size_t)ringPos]=energy;rolling+=energy;ringPos=(ringPos+1)%loudnessWindow;if(ringCount==loudnessWindow&&++hopCount>=hop){blocks.push_back(rolling/loudnessWindow);hopCount=0;}}}
    if(blocks.empty())return result;
    const auto loudness=[](double energy){return energy>0?-0.691+10*std::log10(energy):-100.0;};
    double ungated=0;int count=0;for(double energy:blocks)if(loudness(energy)>-70){ungated+=energy;++count;}if(!count)return result;ungated/=count;const double relative=loudness(ungated)-10;double gated=0;count=0;for(double energy:blocks)if(loudness(energy)>-70&&loudness(energy)>relative){gated+=energy;++count;}if(!count)return result;
    result.valid=true;result.loudnessLufs=loudness(gated/count);result.samplePeakDb=juce::Decibels::gainToDecibels(peak,-100.0f);result.suggestedMasterDb=juce::jlimit(-12.0,12.0,std::min(-14.0-result.loudnessLufs,-1.0-result.samplePeakDb));
    if(result.samplePeakDb>=0){result.rating="MIX IS CLIPPING";result.advice="Lower Master or one or more tracks. The combined processed mix crosses 0 dBFS.";}
    else if(result.loudnessLufs<-20){result.rating="MIX IS QUIET";result.advice="The combined mix is substantially quieter than a typical -14 LUFS reference. Raise Master only within the available peak headroom.";}
    else if(result.loudnessLufs<-16){result.rating="MIX IS A LITTLE QUIET";result.advice="The balance may be fine, but the combined mix is below a typical streaming reference. A modest Master increase may help.";}
    else if(result.loudnessLufs>-10||result.samplePeakDb>-0.5){result.rating="MIX IS HOT";result.advice="Lower Master to preserve headroom; loud platforms may turn this down anyway.";}
    else {result.rating="MIX LEVEL LOOKS HEALTHY";result.advice="The processed mix has a practical overall loudness and peak headroom. Judge musical balance with your ears.";}
    return result;
}
}
