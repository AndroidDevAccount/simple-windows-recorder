// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TrackReverb.h"
#include <cmath>
namespace studio
{
juce::String validReverbStyle(const juce::String& s) { return s=="hall"||s=="flerb"?s:"spring"; }
double reverbTailSeconds(const ReverbSettings& s)
{ return !s.enabled||s.mix<=0?0: (s.style=="hall"?12:s.style=="flerb"?10:6); }
juce::String describeReverb(const ReverbSettings& s)
{
    const auto style=validReverbStyle(s.style);
    juce::String text=style=="spring"?"Spring: a bright, compact, spring-inspired wash with a dispersive, slightly metallic character.":
        style=="hall"?"Hall: a wider, smoother space with a longer decay for vocals and sustained chords.":
        "Flerb: flanged reverb. A slow sweep moves through the reverb tail while the direct sound stays unmodulated.";
    return text+"\n\nMix: "+juce::String(s.mix*100,0)+"% wet. 0% is unchanged; 100% is reverb only. Verb switches this effect on/off independently of the EQ/compressor preset. Stop playback before changing it.\n\nInspired by the EHX Holy Grail's three flavors, not an EHX product or an exact pedal emulation. Spring is a stylized algorithm, not a physical spring-tank model.\n\nPlayback and export only: source recordings stay untouched. Reverb follows the EQ/compressor and adds no buffering delay to the direct sound. WAV export includes the decaying tail. Stop silences playback; restarting clears the tail. No live mic monitoring.";
}
float TrackReverb::Delay::allpass(float x) noexcept
{
    const float y=data[cursor]-0.65f*x;data[cursor]=x+0.65f*y;
    cursor=(cursor+1)%data.size();return y;
}
float TrackReverb::Delay::flange(float x,double delay) noexcept
{
    data[cursor]=x;
    double read=(double)cursor-delay;while(read<0)read+=data.size();
    const auto a=(size_t)read,b=(a+1)%data.size();
    const float y=data[a]+(float)(read-a)*(data[b]-data[a]);
    cursor=(cursor+1)%data.size();return 0.5f*(x+y);
}
void TrackReverb::prepare(const ReverbSettings& s,double sampleRate)
{
    rate=std::max(8000.0,sampleRate);mix=s.enabled?juce::jlimit(0.0f,1.0f,s.mix):0;
    mode=s.style=="hall"?1:s.style=="flerb"?2:0;
    phase=0;low.fill(0);previous.fill(0);high.fill(0);
    juce::Reverb::Parameters p;
    p.roomSize=mode==0?0.38f:mode==1?0.75f:0.65f;p.damping=mode==0?0.3f:0.55f;
    p.wetLevel=1.0f/3.0f;p.dryLevel=0;p.width=mode==0?0.65f:1;p.freezeMode=0;
    tank.setParameters(p);tank.setSampleRate(rate);tank.reset();
    for(int ch=0;ch<2;++ch)
    {
        for(int i=0;i<3;++i)
        {auto& d=dispersion[(size_t)ch][(size_t)i];d.data.assign((size_t)(rate*(0.0031+i*0.0018+ch*0.00017)),0);d.cursor=0;}
        modulation[(size_t)ch].data.assign((size_t)(rate*0.012)+2,0);modulation[(size_t)ch].cursor=0;
    }
    lowCoefficient=(float)(1-std::exp(-juce::MathConstants<double>::twoPi*5500/rate));
    highCoefficient=(float)std::exp(-juce::MathConstants<double>::twoPi*120/rate);
}
void TrackReverb::process(float* left,float* right,int count) noexcept
{
    if(mix<=0)return;
    juce::ScopedNoDenormals noDenormals;
    for(int offset=0;offset<count;offset+=512)
    {
        const int n=std::min(512,count-offset);
        for(int i=0;i<n;++i)for(int ch=0;ch<2;++ch)
        {
            float x=(ch==0?left:right)[offset+i];
            if(mode==0)
            {
                high[(size_t)ch]=highCoefficient*(high[(size_t)ch]+x-previous[(size_t)ch]);previous[(size_t)ch]=x;x=high[(size_t)ch];
                for(auto& d:dispersion[(size_t)ch])x=d.allpass(x);
            }
            wet[(size_t)ch][(size_t)i]=x;
        }
        tank.processStereo(wet[0].data(),wet[1].data(),n);
        for(int i=0;i<n;++i)
        {
            for(int ch=0;ch<2;++ch)
            {
                float x=wet[(size_t)ch][(size_t)i];
                if(mode==0){low[(size_t)ch]+=lowCoefficient*(x-low[(size_t)ch]);x=low[(size_t)ch];}
                if(mode==2)x=modulation[(size_t)ch].flange(x,rate*(0.003+0.002*std::sin(phase+ch*0.8)));
                auto* out=ch==0?left:right;out[offset+i]=(1-mix)*out[offset+i]+mix*x;
            }
            phase+=juce::MathConstants<double>::twoPi*0.17/rate;
            if(phase>=juce::MathConstants<double>::twoPi)phase-=juce::MathConstants<double>::twoPi;
        }
    }
}
}
