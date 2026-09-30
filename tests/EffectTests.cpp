// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/TrackEffects.h"
#include "../src/ProjectStore.h"
#include <cmath>

namespace studio
{
bool runEffectTests(juce::String& report)
{
    const auto fail=[&](const juce::String& s){report+="FAIL effects: "+s+"\n";return false;};
    constexpr int rate=48000, length=48000;
    juce::AudioBuffer<float> original(2,length), whole(2,length), split(2,length);
    for(int i=0;i<length;++i)
    {
        const float v=0.6f*(float)std::sin(juce::MathConstants<double>::twoPi*220*i/rate);
        original.setSample(0,i,v); original.setSample(1,i,v*0.25f);
    }
    for(const auto& preset:effectPresets())
    {
        whole.makeCopyOf(original);split.makeCopyOf(original);
        TrackEffects a,b; a.prepare(preset.id,false,rate); b.prepare(preset.id,false,rate);
        a.process(whole.getWritePointer(0),whole.getWritePointer(1),length);
        for(int pos=0;pos<length;pos+=137)
            b.process(split.getWritePointer(0)+pos,split.getWritePointer(1)+pos,std::min(137,length-pos));
        double difference=0;
        for(int ch=0;ch<2;++ch) for(int i=0;i<length;++i)
        {
            const auto value=whole.getSample(ch,i);
            if(!std::isfinite(value)||std::abs(value-split.getSample(ch,i))>1e-6f) return fail("non-finite or block-dependent processing");
            difference+=std::abs(value-original.getSample(ch,i));
        }
        if(juce::String(preset.id)=="dry" ? difference!=0 : difference<1) return fail("dry changed or preset did nothing");
        for(int i=0;i<length;++i)
            if(std::abs(whole.getSample(1,i)-whole.getSample(0,i)*0.25f)>1e-5f) return fail("stereo image changed");
        a.prepare(preset.id,true,rate);whole.makeCopyOf(original);
        a.process(whole.getWritePointer(0),whole.getWritePointer(1),length);
        for(int ch=0;ch<2;++ch) for(int i=0;i<length;++i)
            if(whole.getSample(ch,i)!=original.getSample(ch,i)) return fail("bypass is not exact");
    }
    report+="PASS effects: all presets change sound; dry/bypass exact; stereo-linked and independent of block size.\n";

    for(const double sampleRate:{44100.0,48000.0,96000.0})
    {
        TrackEffects processor;processor.prepare("lead-vocal",false,sampleRate);
        whole.clear();whole.setSample(0,0,1);whole.setSample(1,0,1);
        processor.process(whole.getWritePointer(0),whole.getWritePointer(1),length);
        if(std::abs(whole.getSample(0,0))<0.1f) return fail("impulse gained a buffering delay");
        for(int i=0;i<length;++i) if(!std::isfinite(whole.getSample(0,i))) return fail("unstable impulse response");
        whole.clear();processor.prepare("bass",false,sampleRate);
        processor.process(whole.getWritePointer(0),whole.getWritePointer(1),length);
        if(whole.getMagnitude(0,length)!=0) return fail("silence produces audio");
    }
    report+="PASS effects: immediate impulse response, stable filters and silent reset at 44.1/48/96 kHz.\n";

    whole.makeCopyOf(original);split.makeCopyOf(original);split.applyGain(0.05f);
    TrackEffects loud,quiet;loud.prepare("lead-vocal",false,rate);quiet.prepare("lead-vocal",false,rate);
    loud.process(whole.getWritePointer(0),whole.getWritePointer(1),length);
    quiet.process(split.getWritePointer(0),split.getWritePointer(1),length);
    const float loudRms=whole.getRMSLevel(0,length/2,length/2),quietRms=split.getRMSLevel(0,length/2,length/2);
    if(quietRms<=0 || loudRms/quietRms>=14.0f) return fail("compressor did not reduce loud/quiet contrast");
    report+="PASS effects: compressor reduces loud passages relative to quiet ones.\n";

    whole.clear();whole.setSample(0,24000,1.4f);whole.setSample(1,24000,0.7f);
    TrackEffects tamer;tamer.prepare("peak-tamer",false,rate);tamer.process(whole.getWritePointer(0),whole.getWritePointer(1),length);
    const float ceiling=juce::Decibels::decibelsToGain(-1.0f);
    if(whole.getMagnitude(0,length)>ceiling+1e-5f||whole.getSample(0,24000)<0.5f||std::abs(whole.getSample(1,24000)/whole.getSample(0,24000)-0.5f)>1e-5f)return fail("peak tamer ceiling or stereo link failed");
    whole.clear();whole.setSample(0,24000,0.2f);whole.setSample(1,24000,0.1f);tamer.prepare("peak-tamer",false,rate);tamer.process(whole.getWritePointer(0),whole.getWritePointer(1),length,10.0f);
    if(whole.getMagnitude(0,length)>ceiling+1e-5f||std::abs(whole.getSample(1,24000)/whole.getSample(0,24000)-0.5f)>1e-5f)return fail("track Gain was applied after Peak tamer ceiling");
    whole.clear();whole.setSample(0,24000,0.5f);whole.setSample(1,24000,0.25f);tamer.prepare("acoustic-guitar",false,rate,{},true);tamer.process(whole.getWritePointer(0),whole.getWritePointer(1),length,4.0f);
    if(whole.getMagnitude(0,length)>ceiling+1e-5f)return fail("independent Peak tamer did not follow the Acoustic guitar preset");
    report+="PASS effects: Peak tamer catches isolated overloads after track Gain at -1 dBFS without changing stereo balance.\n";

    // Steady DC must be removed by the high-pass, without a noise gate.
    whole.clear();for(int ch=0;ch<2;++ch)juce::FloatVectorOperations::fill(whole.getWritePointer(ch),0.2f,length);
    TrackEffects dc;dc.prepare("lead-vocal",false,rate);dc.process(whole.getWritePointer(0),whole.getWritePointer(1),length);
    if(whole.getMagnitude(0,length/2,length/2)>0.001f) return fail("rumble filter did not remove DC");

    const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("recorder-fx-test-"+juce::Uuid().toString());
    if(folder.createDirectory().failed())return fail("cannot create fixture");
    struct Cleanup {juce::File file;~Cleanup(){if(file.getFileName().startsWith("recorder-fx-test-"))file.deleteRecursively();}} cleanup{folder};
    Session session; session.countInBars=0;
    Track track; track.id="fx";track.name="Test vocal";track.effectPresetId="lead-vocal";
    Clip clip;clip.id="tone";clip.file=folder.getChildFile("source.wav");clip.audio=std::make_shared<juce::AudioBuffer<float>>(original);
    clip.sampleRate=rate;clip.lengthSeconds=1;track.clips.push_back(clip);session.tracks.push_back(track);
    {
        juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream(clip.file.createOutputStream());
        auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if(!writer || !writer->writeFromAudioSampleBuffer(original,0,length))return fail("cannot write source fixture");
    }
    const auto project=folder.getChildFile("song.srproject");
    for(bool bypass:{false,true})
    {
        session.tracks[0].effectsBypassed=bypass;
        if(saveProject(session,project,0).failed()) return fail("cannot save preset");
        Session restored;double position=0;
        if(loadProject(restored,project,position).failed()||restored.tracks[0].effectPresetId!="lead-vocal"||restored.tracks[0].effectsBypassed!=bypass)
            return fail("preset/bypass did not survive reload");
    }
    auto json=juce::JSON::parse(project);
    json["tracks"][0].getDynamicObject()->removeProperty("effectPreset");
    json["tracks"][0].getDynamicObject()->removeProperty("effectsBypassed");
    project.replaceWithText(juce::JSON::toString(json));
    Session legacy;double position=0;
    if(loadProject(legacy,project,position).failed()||legacy.tracks[0].effectPresetId!="dry"||!legacy.tracks[0].effectsBypassed)return fail("legacy project did not default to FX off");
    json=juce::JSON::parse(project);auto* legacyTrack=json["tracks"][0].getDynamicObject();legacyTrack->setProperty("effectPreset","peak-tamer");legacyTrack->setProperty("effectsBypassed",false);legacyTrack->removeProperty("peakTamer");project.replaceWithText(juce::JSON::toString(json));
    Session migrated;if(loadProject(migrated,project,position).failed()||migrated.tracks[0].effectPresetId!="dry"||!migrated.tracks[0].effectsBypassed||!migrated.tracks[0].peakTamerEnabled)return fail("old active Peak tamer preset was not migrated to the independent switch");
    report+="PASS effects: preset and bypass persist; old projects load safely and migrate active Peak tamer; DC removed.\n";

    session.tracks[0].effectsBypassed=false;
    const auto mix=folder.getChildFile("mix.wav");
    if(exportMix(session,mix,rate).failed()) return fail("cannot export effected audio");
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(mix));
    if(!reader || reader->lengthInSamples!=length) return fail("export changed duration");
    whole.clear();reader->read(&whole,0,length,0,true,true);
    SessionEngine engine;engine.prepareForDevice(rate,257,2);engine.session()=session;
    if(!engine.play(0))return fail("cannot start simulated playback");
    split.clear();std::array<float,257> left{},right{};float* outs[]{left.data(),right.data()};
    const float* ins[]{nullptr,nullptr};
    for(int pos=0;pos<length;pos+=257)
    {
        const int n=std::min(257,length-pos);
        engine.audioDeviceIOCallbackWithContext(ins,2,outs,2,n,{});
        split.copyFrom(0,pos,left.data(),n);split.copyFrom(1,pos,right.data(),n);
    }
    engine.audioDeviceStopped();
    for(int ch=0;ch<2;++ch)for(int i=0;i<length;++i)
    {
        if(std::abs(whole.getSample(ch,i)-split.getSample(ch,i))>3e-5f)return fail("playback and export differ");
        if(clip.audio->getSample(ch,i)!=original.getSample(ch,i))return fail("source audio mutated");
    }
    report+="PASS effects: real engine playback matches exported WAV across different block sizes; duration/source audio unchanged.\n";
    return true;
}
}
