// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/InputAnalysis.h"
#include "../src/SessionEngine.h"
#include <cmath>
namespace studio
{
bool runFeedbackTests(juce::String& report)
{
    const auto fail=[&](const juce::String& s){report+="FAIL feedback: "+s+"\n";return false;};
    std::array<float,2048> samples{};
    for(double frequency:{82.4069,110.0,146.832,195.998,246.942,329.628,440.0,880.0})
    for(double detune:{-20.0,0.0,20.0})
    {
        const double hz=frequency*std::pow(2.0,detune/1200);
        for(int i=0;i<2048;++i){const double phase=juce::MathConstants<double>::twoPi*hz*i/12000;
            samples[(size_t)i]=(float)(0.22*std::sin(phase)+0.12*std::sin(2*phase)+0.07*std::sin(3*phase));}
        const auto pitch=detectPitch(samples.data(),2048,12000);
        if(pitch.midi<0||std::abs(1200*std::log2(pitch.hz/hz))>5)return fail("harmonic-rich pitch inaccurate at "+juce::String(hz,1)+" Hz: "+juce::String(pitch.hz,1));
    }
    samples.fill(0);if(detectPitch(samples.data(),2048,12000).midi>=0)return fail("silence has pitch");
    juce::Random random(9876);for(auto& x:samples)x=(random.nextFloat()-0.5f)*0.5f;
    if(detectPitch(samples.data(),2048,12000).midi>=0)return fail("noise has confident pitch");
    if(pitchName(69)!="A4"||pitchName(40)!="E2")return fail("wrong note names");
    report+="PASS feedback: guitar/voice-range harmonic fixtures and +/-20 cents within 5 cents; silence/noise rejected; note names correct.\n";
    for(double rate:{44100.0,48000.0,96000.0})
    {
        InputAnalysis analysis;analysis.prepare(rate);analysis.setInput(1);
        std::vector<float> source((size_t)(rate*0.25));
        for(size_t i=0;i<source.size();++i)source[i]=(float)(0.3*std::sin(juce::MathConstants<double>::twoPi*110*i/rate));
        analysis.capture(source.data(),(int)source.size(),0);PitchResult p;
        if(analysis.poll(p))return fail("tuner listened to wrong channel");
        analysis.capture(source.data(),(int)source.size(),1);
        if(!analysis.poll(p)||p.midi!=45||std::abs(p.cents)>3)return fail("resampled tuner input failed");
        analysis.setInput(0);analysis.capture(nullptr,(int)source.size(),0);
        if(!analysis.poll(p)||p.midi>=0)return fail("missing input didn't clear pitch");
        for(int i=0;i<12;++i)analysis.capture(source.data(),(int)source.size(),0);
        if(!analysis.poll(p)||p.midi>=0)return fail("stale tuner backlog displayed as current");
    }
    report+="PASS feedback: tuner channel isolation, missing-input silence and device rates 44.1/48/96 kHz.\n";
    for(double bpm:{100.0,120.0})
    {
        SessionEngine engine;engine.prepareForDevice(48000,1024,2);engine.session().bpm=bpm;engine.session().metronome=true;
        if(!engine.play(0))return fail("cannot play metronome");
        std::array<float,1024> left{},right{};float* out[]{left.data(),right.data()};const float* in[]{nullptr,nullptr};
        for(int block=0;block<60;++block)
        {
            engine.audioDeviceIOCallbackWithContext(in,2,out,2,1024,{});
            for(int i=0;i<1024;++i)if(std::abs(left[(size_t)i]-metronomeSample(block*1024+i,48000*60/bpm,48000))>1e-6f)return fail("visible/audible click source diverged");
        }
        engine.audioDeviceStopped();
    }
    AnalysisQueue<int,5> queue;for(int i=0;i<4;++i)if(!queue.push(i))return fail("queue capacity");
    if(queue.push(5))return fail("overflow not bounded");
    for(int i=0;i<4;++i){int x=-1;if(!queue.pop(x)||x!=i)return fail("queue order");}
    report+="PASS feedback: shared click signal matches playback at 100/120 BPM; telemetry overflow bounded.\n";
    return true;
}
}
