// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
namespace studio
{
struct ReverbSettings { bool enabled=false; juce::String style{"spring"}; float mix=0.2f; };
juce::String validReverbStyle(const juce::String&);
double reverbTailSeconds(const ReverbSettings&);
juce::String describeReverb(const ReverbSettings&);
class TrackReverb
{
public:
    void prepare(const ReverbSettings&,double rate);
    void process(float*,float*,int) noexcept;
private:
    struct Delay
    {
        std::vector<float> data;
        size_t cursor=0;
        float allpass(float x) noexcept;
        float flange(float x,double delaySamples) noexcept;
    };
    juce::Reverb tank;
    std::array<std::array<Delay,3>,2> dispersion;
    std::array<Delay,2> modulation;
    std::array<std::array<float,512>,2> wet {};
    std::array<float,2> low {},previous {},high {};
    double rate=48000,phase=0;
    float mix=0,lowCoefficient=0,highCoefficient=0;
    int mode=0;
};
bool runReverbTests(juce::String&);
}
