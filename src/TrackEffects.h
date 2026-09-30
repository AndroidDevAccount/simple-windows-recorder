// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <array>
#include "TrackReverb.h"

namespace studio
{
struct EffectPreset
{
    const char* id;
    const char* name;
    const char* purpose;
    float highPass, lowMidHz, lowMidDb, presenceHz, presenceDb;
    float thresholdDb, ratio, attackMs, releaseMs;
};
const std::array<EffectPreset, 7>& effectPresets();
const EffectPreset& effectPreset(const juce::String& id);
juce::String describeEffectPreset(const juce::String& id);

// Construct/prepare off the audio thread. No allocations, locks, lookahead or
// block buffering in process(). Stereo-linked dynamics keep the image stable.
class TrackEffects
{
public:
    void prepare(const juce::String& presetId,bool bypass,double sampleRate,const ReverbSettings& reverb={},bool peakTamer=false);
    void process(float* left,float* right,int count,float postGain=1.0f) noexcept;
private:
    struct Biquad
    {
        std::array<float, 6> c {1,0,0,1,0,0};
        float z1=0, z2=0;
        float tick(float x) noexcept;
    };
    std::array<std::array<Biquad, 3>, 2> filters;
    bool dry=true,tamePeaks=false;
    float envelope=0,attack=0,release=0,threshold=-18,slope=0,limiterCeiling=2.0f,tamerEnvelope=0,tamerAttack=0,tamerRelease=0;
    std::unique_ptr<TrackReverb> reverb;
};
struct Track;
// Shared dry renderer ensures playback/export have identical clip boundaries.
void renderTrackAudio(const Track&, double start, double rate, float* left, float* right, int count) noexcept;
bool runEffectTests(juce::String&);
}
