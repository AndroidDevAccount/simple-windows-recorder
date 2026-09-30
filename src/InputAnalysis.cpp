// SPDX-License-Identifier: AGPL-3.0-or-later
#include "InputAnalysis.h"
#include <cmath>
namespace studio
{
float metronomeSample(double frame,double framesPerBeat,double rate) noexcept
{
    const auto beat=(int64_t)std::floor(frame/framesPerBeat);const double offset=frame-beat*framesPerBeat;
    const double length=rate*0.016;if(offset<0||offset>=length)return 0;
    const double env=1-offset/length;return (float)((beat%4==0?0.16:0.11)*env*env*std::sin(juce::MathConstants<double>::twoPi*(beat%4==0?1400:1000)*offset/rate));
}
PitchResult detectPitch(const float* samples,int count,double rate)
{
    PitchResult result;if(count<1024||rate<=0)return result;
    double energy=0,mean=0;for(int i=0;i<count;++i){energy+=samples[i]*samples[i];mean+=samples[i];}
    if(energy/count<0.00001)return result;
    mean/=count;
    const int maxLag=std::min(count/2,(int)(rate/60)),minLag=std::max(2,(int)(rate/1100));
    std::array<double,1025> difference{};
    if(maxLag>=1024)return result;
    double cumulative=0;
    for(int lag=1;lag<=maxLag;++lag)
    {
        double sum=0;for(int i=0;i<count-maxLag;++i){const double d=(samples[i]-mean)-(samples[i+lag]-mean);sum+=d*d;}
        cumulative+=sum;difference[(size_t)lag]=cumulative>1e-14?sum*lag/cumulative:1;
    }
    for(int lag=minLag;lag<maxLag-1;++lag)
    {
        if(difference[(size_t)lag]>0.15)continue;
        while(lag<maxLag-1&&difference[(size_t)lag+1]<difference[(size_t)lag])++lag;
        const double a=difference[(size_t)lag-1],b=difference[(size_t)lag],c=difference[(size_t)lag+1];
        const double denom=a-2*b+c;const double offset=std::abs(denom)>1e-12?juce::jlimit(-0.5,0.5,0.5*(a-c)/denom):0;
        result.hz=rate/(lag+offset);const double note=69+12*std::log2(result.hz/440);
        result.midi=(int)std::llround(note);result.cents=100*(note-result.midi);result.confidence=(float)(1-b);return result;
    }
    return result;
}
juce::String pitchName(int midi)
{ if(midi<0)return "--";static const char* names[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};return juce::String(names[midi%12])+juce::String(midi/12-1); }
void InputAnalysis::prepare(double rate) noexcept
{ sampleRate=rate;decimation=std::max(1,(int)std::round(rate/12000));used=partial=0;sum=0;active=-1;epoch.fetch_add(1); }
void InputAnalysis::capture(const float* samples,int count,int channel) noexcept
{
    if(channel!=selected.load())return;
    if(active!=channel){active=channel;used=partial=0;sum=0;}
    for(int i=0;i<count;++i)
    {
        sum+=samples?samples[i]:0;
        if(++partial<decimation)continue;
        frame.samples[(size_t)used++]=sum/decimation;sum=0;partial=0;
        if(used==2048){frame.channel=channel;frame.epoch=epoch.load();frame.sequence=sequence.fetch_add(1)+1;frame.rate=sampleRate/decimation;queue.push(frame);used=0;}
    }
}
bool InputAnalysis::poll(PitchResult& result)
{
    Frame latest,candidate;bool found=false;
    while(queue.pop(candidate))if(candidate.channel==selected.load()&&candidate.epoch==epoch.load()){latest=candidate;found=true;}
    if(found)result=sequence.load()-latest.sequence>1?PitchResult{}:detectPitch(latest.samples.data(),2048,latest.rate);
    return found;
}
}
