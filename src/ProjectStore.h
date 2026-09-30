// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SessionEngine.h"

namespace studio
{
struct RecordingLevelAnalysis
{
    bool valid=false;double peakDb=-100,activeRmsDb=-100;juce::String rating,advice;
};
struct MixLevelAnalysis
{
    bool valid=false;double loudnessLufs=-100,samplePeakDb=-100,suggestedMasterDb=0;juce::String rating,advice;
};
juce::Result saveProject(const Session&, const juce::File&, double playhead);
juce::Result loadProject(Session&, const juce::File&, double& playhead);
juce::Result importAudio(Clip&, const juce::File& source, const juce::File& mediaDirectory);
juce::Result exportMix(const Session&, const juce::File& destination, double sampleRate);
RecordingLevelAnalysis analyseRecordingLevel(const Clip&);
MixLevelAnalysis analyseMixLevel(const Session&,double sampleRate=48000.0);
}
