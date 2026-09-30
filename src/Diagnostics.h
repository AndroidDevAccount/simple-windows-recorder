// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
namespace studio
{
void startDiagnostics();
void logEvent(const juce::String&);
juce::File diagnosticFile();
void showDiagnostics(const juce::String& context);
}
