// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TrackEffects.h"
#include "SessionEngine.h"
#include <cmath>

namespace studio
{
const std::array<EffectPreset, 6>& effectPresets()
{
    static const std::array<EffectPreset, 6> presets {{
        {"dry", "Dry / no effects", "Your original sound, unchanged.", 0,0,0,0,0,0,1,0,0},
        {"lead-vocal", "Lead vocal", "A clearer vocal with less rumble and more even loud phrases.", 80,250,-2,3500,2,-18,3,10,100},
        {"warm-vocal", "Warm vocal", "A gentler vocal sound with less low-mid muddiness and a small clarity lift.", 65,300,-1.5f,2500,1,-20,2,20,140},
        {"acoustic-guitar", "Acoustic guitar", "Less boom, a little more string definition, and gentle control of strums.", 70,220,-2.5f,4000,2,-18,2,25,120},
        {"bass", "Bass", "Keep the low end, reduce muddy mids, and even out stronger notes.", 30,300,-2,1200,1.5f,-20,4,20,150},
        {"gentle-cleanup", "Gentle cleanup", "A light starting point for other instruments: less rumble and small level changes.", 40,300,-1,3000,1,-16,1.5f,25,150}
    }};
    return presets;
}
const EffectPreset& effectPreset(const juce::String& id)
{
    for(const auto& p:effectPresets()) if(id==p.id) return p;
    return effectPresets().front();
}
juce::String describeEffectPreset(const juce::String& id)
{
    const auto& p=effectPreset(id);
    juce::String text(p.purpose);
    if(p.highPass>0)
    {
        text += "\n\n1. Rumble filter: high-pass at " + juce::String(p.highPass,0) + " Hz (12 dB/octave). Reduces deep rumble below this range.";
        text += "\n2. Body EQ: " + juce::String(p.lowMidDb,1) + " dB at " + juce::String(p.lowMidHz,0) + " Hz. Reduces boom and muddiness.";
        text += "\n3. Clarity EQ: +" + juce::String(p.presenceDb,1) + " dB at " + juce::String(p.presenceHz,0) + " Hz. Helps detail come through. Both EQ bands use Q 0.8.";
        text += "\n4. Compressor: " + juce::String(p.ratio,1) + ":1 above " + juce::String(p.thresholdDb,0) + " dBFS; attack " + juce::String(p.attackMs,0) + " ms, release " + juce::String(p.releaseMs,0) + " ms. Turns down louder passages; no automatic makeup gain.";
    }
    text += "\n\nApplies to this track's playback and WAV export only. Original recordings stay untouched. FX off bypasses the preset for comparison. Stop playback before changing it. No added buffering latency; no reverb, gate or live mic monitoring. These are starting points, not automatic fixes for every recording.";
    return text;
}
float TrackEffects::Biquad::tick(float x) noexcept
{
    const float y=c[0]*x+z1;
    z1=c[1]*x-c[4]*y+z2; z2=c[2]*x-c[5]*y;
    return y;
}
void TrackEffects::prepare(const juce::String& id, bool bypass, double rate)
{
    const auto& p=effectPreset(id);
    dry=bypass || p.highPass==0;
    envelope=0;
    for(auto& channel:filters) for(auto& filter:channel) filter=Biquad{};
    if(dry) return;
    rate=std::max(8000.0,rate);
    using Coeff=juce::dsp::IIR::ArrayCoefficients<float>;
    const auto freq=[rate](float hz){return std::min(hz,(float)(rate*0.45));};
    const std::array<std::array<float,6>,3> coefficients {{
        Coeff::makeHighPass(rate,freq(p.highPass)),
        Coeff::makePeakFilter(rate,freq(p.lowMidHz),0.8f,juce::Decibels::decibelsToGain(p.lowMidDb)),
        Coeff::makePeakFilter(rate,freq(p.presenceHz),0.8f,juce::Decibels::decibelsToGain(p.presenceDb))
    }};
    for(auto& channel:filters) for(size_t i=0;i<3;++i)
    { channel[i].c=coefficients[i]; const float a0=channel[i].c[3]; for(auto& c:channel[i].c)c/=a0; }
    attack=(float)std::exp(-1.0/(rate*p.attackMs*0.001));
    release=(float)std::exp(-1.0/(rate*p.releaseMs*0.001));
    threshold=p.thresholdDb; slope=1.0f/p.ratio-1.0f;
}
void TrackEffects::process(float* left,float* right,int count) noexcept
{
    if(dry) return;
    juce::ScopedNoDenormals noDenormals;
    for(int i=0;i<count;++i)
    {
        float l=left[i], r=right[i];
        for(auto& f:filters[0]) l=f.tick(l);
        for(auto& f:filters[1]) r=f.tick(r);
        const float peak=std::max(std::abs(l),std::abs(r));
        const float coefficient=peak>envelope?attack:release;
        envelope=coefficient*envelope+(1-coefficient)*peak;
        const float db=juce::Decibels::gainToDecibels(envelope,-100.0f);
        const float gain=juce::Decibels::decibelsToGain(std::max(0.0f,db-threshold)*slope);
        left[i]=l*gain; right[i]=r*gain;
    }
}
void renderTrackAudio(const Track& track,double start,double rate,float* left,float* right,int count) noexcept
{
    juce::FloatVectorOperations::clear(left,count); juce::FloatVectorOperations::clear(right,count);
    const double end=start+count/rate;
    for(const auto& c:track.clips)
    {
        if(!c.audio || c.audio->getNumChannels()==0 || c.audio->getNumSamples()==0 || c.sampleRate<=0
           || c.startSeconds>=end || c.startSeconds+c.lengthSeconds<=start) continue;
        const int first=juce::jlimit(0,count,(int)std::ceil((c.startSeconds-start)*rate-1.0e-6));
        const int last=juce::jlimit(0,count,(int)std::ceil((c.startSeconds+c.lengthSeconds-start)*rate-1.0e-6));
        for(int ch=0;ch<2;++ch)
        {
            auto* out=ch==0?left:right;
            const auto* data=c.audio->getReadPointer(std::min(ch,c.audio->getNumChannels()-1));
            for(int i=first;i<last;++i)
            {
                const double time=start+i/rate-c.startSeconds;
                const double source=(c.sourceOffsetSeconds+time)*c.sampleRate;
                const auto index=(int64_t)std::floor(source+1.0e-7);
                if(index<0 || index>=c.audio->getNumSamples()) continue;
                const auto next=std::min<int64_t>(index+1,c.audio->getNumSamples()-1);
                const float fraction=(float)std::max(0.0,source-index);
                const float edge=(float)juce::jlimit(0.0,1.0,std::min(time,c.lengthSeconds-time)/0.003);
                out[i]+=(data[index]+fraction*(data[next]-data[index]))*(float)c.gain*edge;
            }
        }
    }
}
}
