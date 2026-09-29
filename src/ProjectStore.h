// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SessionEngine.h"

namespace studio
{
juce::Result saveProject(const Session&, const juce::File&, double playhead);
juce::Result loadProject(Session&, const juce::File&, double& playhead);
juce::Result importAudio(Clip&, const juce::File& source, const juce::File& mediaDirectory);
juce::Result exportMix(const Session&, const juce::File& destination, double sampleRate);
}
