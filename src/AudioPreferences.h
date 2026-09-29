// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>

namespace studio
{
struct AudioSetupResult
{
    juce::String message;
    double compensationMs = 0.0;
    bool calibrated = false;
};

struct AudioDeviceIdentity
{
    juce::String backend, input, output, inputMask, outputMask;
    double sampleRate = 0.0;
    int bufferSize = 0;
    bool operator==(const AudioDeviceIdentity&) const;
};

juce::File audioPreferencesFile();
std::unique_ptr<juce::PropertiesFile> openAudioPreferences();
AudioDeviceIdentity currentAudioIdentity(juce::AudioDeviceManager&);
AudioSetupResult restoreAudioSetup(juce::AudioDeviceManager&);
AudioSetupResult calibrationForActiveSetup(juce::AudioDeviceManager&);
juce::Result saveAudioSetup(juce::AudioDeviceManager&, double compensationMs, bool calibrated);
bool runPreferenceTests(juce::String& report);
}
