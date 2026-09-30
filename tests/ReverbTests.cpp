// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/TrackEffects.h"
#include "../src/ProjectStore.h"
#include <cmath>
namespace studio
{
bool runReverbTests(juce::String& report)
{
    const auto fail=[&](const juce::String& s){report+="FAIL reverb: "+s+"\n";return false;};
    for(double rate:{44100.0,48000.0,96000.0})for(const auto* style:{"spring","hall","flerb"})
    {
        const int length=(int)(rate*13);
        juce::AudioBuffer<float> a(2,length),b(2,length);a.clear();b.clear();
        a.setSample(0,0,0.8f);a.setSample(1,0,0.8f);b.makeCopyOf(a);
        ReverbSettings settings;settings.enabled=true;settings.style=style;settings.mix=0.3f;
        TrackReverb x,y;x.prepare(settings,rate);y.prepare(settings,rate);
        x.process(a.getWritePointer(0),a.getWritePointer(1),length);
        for(int pos=0;pos<length;pos+=137)y.process(b.getWritePointer(0)+pos,b.getWritePointer(1)+pos,std::min(137,length-pos));
        if(std::abs(a.getSample(0,0)-0.56f)>1e-6f)return fail("direct sound was delayed");
        if(a.getMagnitude((int)(rate*0.05),(int)rate)<1e-5f)return fail("no audible impulse tail");
        if(a.getMagnitude(length-(int)rate,(int)rate)>1e-4f)return fail("tail failed to decay");
        for(int ch=0;ch<2;++ch)for(int i=0;i<length;++i)
            if(!std::isfinite(a.getSample(ch,i))||std::abs(a.getSample(ch,i)-b.getSample(ch,i))>1e-6f)return fail("unstable or block-dependent output");
        settings.mix=0;x.prepare(settings,rate);a.clear();a.setSample(0,0,0.8f);x.process(a.getWritePointer(0),a.getWritePointer(1),length);
        if(a.getSample(0,0)!=0.8f||a.getMagnitude(1,length-1)!=0)return fail("zero mix is not dry");
        settings.mix=0.3f;settings.enabled=false;x.prepare(settings,rate);x.process(a.getWritePointer(0),a.getWritePointer(1),length);
        if(a.getSample(0,0)!=0.8f||a.getMagnitude(1,length-1)!=0)return fail("bypass is not dry");
        settings.enabled=true;settings.mix=1;x.prepare(settings,rate);a.clear();a.setSample(0,0,0.8f);x.process(a.getWritePointer(0),a.getWritePointer(1),length);
        if(a.getSample(0,0)!=0)return fail("100 percent wet leaked direct sound");
        x.prepare(settings,rate);a.clear();x.process(a.getWritePointer(0),a.getWritePointer(1),1024);
        if(a.getMagnitude(0,1024)!=0)return fail("reset retained old tail");
    }
    report+="PASS reverb: Spring/Hall/Flerb at 44.1/48/96 kHz; finite decaying tails; exact bypass/zero mix; wet-only; reset; no direct delay; block-size independence.\n";
    const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("recorder-reverb-test-"+juce::Uuid().toString());
    if(folder.createDirectory().failed())return fail("cannot create fixture");
    struct Cleanup{juce::File f;~Cleanup(){if(f.getFileName().startsWith("recorder-reverb-test-"))f.deleteRecursively();}} cleanup{folder};
    Session session;Track track;track.id="verb";track.effectPresetId="lead-vocal";track.reverb={true,"hall",0.3f};
    Clip clip;clip.id="impulse";clip.lengthSeconds=0.1;clip.audio=std::make_shared<juce::AudioBuffer<float>>(1,4800);clip.audio->clear();clip.audio->setSample(0,2400,0.8f);
    clip.file=folder.getChildFile("source.wav");
    {juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream(clip.file.createOutputStream());auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
    if(!writer||!writer->writeFromAudioSampleBuffer(*clip.audio,0,4800))return fail("fixture write");}
    track.clips.push_back(clip);session.tracks.push_back(track);
    const auto project=folder.getChildFile("song.srproject");Session loaded;double position=0;
    if(saveProject(session,project,0).failed()||loadProject(loaded,project,position).failed()||!loaded.tracks[0].reverb.enabled||loaded.tracks[0].reverb.style!="hall"||std::abs(loaded.tracks[0].reverb.mix-0.3f)>1e-6f)return fail("settings persistence");
    auto json=juce::JSON::parse(project);for(const auto* key:{"reverbEnabled","reverbStyle","reverbMix"})json["tracks"][0].getDynamicObject()->removeProperty(key);
    project.replaceWithText(juce::JSON::toString(json));
    if(loadProject(loaded,project,position).failed()||loaded.tracks[0].reverb.enabled)return fail("old project not dry");
    const auto output=folder.getChildFile("mix.wav");if(exportMix(session,output,48000).failed())return fail("export failed");
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(output));
    if(!reader||reader->lengthInSamples!=580800)return fail("export missing 12-second tail");
    juce::AudioBuffer<float> audio(2,(int)reader->lengthInSamples);reader->read(&audio,0,audio.getNumSamples(),0,true,true);
    if(audio.getMagnitude(4800,48000)<1e-5f)return fail("export tail silent");
    SessionEngine engine;engine.prepareForDevice(48000,257,2);engine.session()=session;if(!engine.play(0))return fail("engine play");
    std::array<float,257> left{},right{};float* outputs[]{left.data(),right.data()};const float* inputs[]{nullptr,nullptr};
    for(int pos=0;pos<audio.getNumSamples();pos+=257)
    {
        const int n=std::min(257,audio.getNumSamples()-pos);engine.audioDeviceIOCallbackWithContext(inputs,2,outputs,2,n,{});
        for(int i=0;i<n;++i)for(int ch=0;ch<2;++ch)
        {const float fade=(float)juce::jlimit(0.0,1.0,(12.1-(pos+i)/48000.0)/0.1);
        if(std::abs(audio.getSample(ch,pos+i)-outputs[ch][i]*fade)>3e-5f)return fail("engine/export tail mismatch");}
    }
    engine.audioDeviceStopped();
    if(clip.audio->getSample(0,2400)!=0.8f)return fail("source changed");
    report+="PASS reverb: settings persist, legacy projects stay dry, export retains tail and matches playback; original audio unchanged.\n";
    return true;
}
}
