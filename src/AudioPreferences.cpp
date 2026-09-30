// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AudioPreferences.h"
#include "Diagnostics.h"
#include <cmath>

namespace studio
{
namespace
{
juce::PropertiesFile::Options preferenceOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "SimpleRecorder";
    options.filenameSuffix = ".settings";
    options.folderName = "SimpleWindowsRecorder";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

AudioDeviceIdentity readIdentity(const juce::PropertySet& settings)
{
    return { settings.getValue("calibrationBackend"), settings.getValue("calibrationInput"),
             settings.getValue("calibrationOutput"), settings.getValue("calibrationInputMask"),
             settings.getValue("calibrationOutputMask"), settings.getDoubleValue("calibrationSampleRate"),
             settings.getIntValue("calibrationBufferSize") };
}

int chooseDevice(const juce::StringArray& names, const juce::String& saved, const juce::String& fallback)
{
    const auto exact = names.indexOf(saved);
    if (exact >= 0) return exact;
    for (int i = 0; i < names.size(); ++i)
        if (names[i].containsIgnoreCase(fallback)) return i;
    return names.isEmpty() ? -1 : 0;
}
}

bool AudioDeviceIdentity::operator==(const AudioDeviceIdentity& other) const
{
    return backend == other.backend && input == other.input && output == other.output
        && inputMask == other.inputMask && outputMask == other.outputMask
        && std::abs(sampleRate - other.sampleRate) < 0.5 && bufferSize == other.bufferSize;
}

juce::File audioPreferencesFile() { return preferenceOptions().getDefaultFile(); }

std::unique_ptr<juce::PropertiesFile> openAudioPreferences()
{
    return std::make_unique<juce::PropertiesFile>(preferenceOptions());
}

AudioDeviceIdentity currentAudioIdentity(juce::AudioDeviceManager& manager)
{
    const auto setup = manager.getAudioDeviceSetup();
    auto* device = manager.getCurrentAudioDevice();
    if (device == nullptr) return {};
    return { manager.getCurrentAudioDeviceType(), setup.inputDeviceName, setup.outputDeviceName,
             device->getActiveInputChannels().toString(16), device->getActiveOutputChannels().toString(16),
             device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples() };
}

juce::Result saveAudioSetup(juce::AudioDeviceManager& manager, double delay, bool calibrated)
{
    if (manager.getCurrentAudioDevice() == nullptr)
        return juce::Result::fail("No audio device is open. Choose your input and output first.");
    auto settings = openAudioPreferences();
    const auto identity = currentAudioIdentity(manager);
    settings->setValue("audioBackend", identity.backend);
    settings->setValue("inputDevice", identity.input);
    settings->setValue("outputDevice", identity.output);
    settings->setValue("sampleRate", identity.sampleRate);
    settings->setValue("bufferSize", identity.bufferSize);
    settings->setValue("inputMask", identity.inputMask);
    settings->setValue("outputMask", identity.outputMask);
    settings->setValue("profileVersion", 2);
    settings->setValue("hasCalibration", calibrated && std::isfinite(delay) && delay >= 0.0);
    if (calibrated && std::isfinite(delay) && delay >= 0.0)
    {
        settings->setValue("calibrationMs", delay);
        settings->setValue("calibrationBackend", identity.backend);
        settings->setValue("calibrationInput", identity.input);
        settings->setValue("calibrationOutput", identity.output);
        settings->setValue("calibrationSampleRate", identity.sampleRate);
        settings->setValue("calibrationBufferSize", identity.bufferSize);
        settings->setValue("calibrationInputMask", identity.inputMask);
        settings->setValue("calibrationOutputMask", identity.outputMask);
    }
    return settings->saveIfNeeded() ? juce::Result::ok()
                                   : juce::Result::fail("Could not save audio settings to " + settings->getFile().getFullPathName());
}

static AudioSetupResult evaluateSavedCalibration(const juce::PropertySet&, const AudioDeviceIdentity&);

AudioSetupResult calibrationForActiveSetup(juce::AudioDeviceManager& manager)
{
    const auto settings = openAudioPreferences();
    const auto actual = currentAudioIdentity(manager);
    return evaluateSavedCalibration(*settings, actual);
}

static AudioSetupResult evaluateSavedCalibration(const juce::PropertySet& properties, const AudioDeviceIdentity& actual)
{
    const auto* settings = &properties;
    AudioSetupResult result;
    if (actual.backend.isEmpty())
    {
        result.message = "Audio is unavailable. Open Audio setup to choose your devices.";
        return result;
    }
    const auto delay = settings->getDoubleValue("calibrationMs", -1.0);
    if (settings->getIntValue("profileVersion") >= 2)
    {
        result.calibrated = settings->getBoolValue("hasCalibration") && std::isfinite(delay)
                         && delay >= 0.0 && actual == readIdentity(*settings);
        result.message = result.calibrated
            ? "Saved timing correction: " + juce::String(delay, 1) + " ms."
            : "No calibration for these device settings. Open Audio setup to measure the delay.";
    }
    else
    {
        // The first prototype saved only endpoint names and delay. Its fixed request
        // was 48 kHz / 256 samples. Preserve that accepted result only for this exact
        // path; the message makes its legacy origin explicit. The next Save binds
        // it to all actual device settings, including the two enabled inputs.
        result.calibrated = actual.backend == settings->getValue("audioBackend")
                         && actual.input == settings->getValue("inputDevice")
                         && actual.output == settings->getValue("outputDevice")
                         && std::abs(actual.sampleRate - 48000.0) < 0.5
                         && actual.bufferSize == 256 && std::isfinite(delay) && delay > 0.0;
        result.message = result.calibrated
            ? "Imported saved timing correction: " + juce::String(delay, 1) + " ms. Confirm alignment with an overdub."
            : "Device settings differ from the old calibration. Open Audio setup to measure this path.";
    }
    result.compensationMs = result.calibrated ? delay : 0.0;
    return result;
}

static AudioSetupResult restoreAudioSetupFrom(juce::AudioDeviceManager& manager,const juce::PropertySet* settings)
{
    // Enumerate without opening a default driver first.
    auto& types = manager.getAvailableDeviceTypes();
    juce::StringArray names;
    for (auto* type : types) names.add(type->getTypeName());
    auto index = names.indexOf(settings->getValue("audioBackend"));
    const bool savedBackendAvailable = index >= 0;
    if (index < 0) index = names.indexOf("ASIO");
    if (index < 0) index = names.indexOf(manager.getCurrentAudioDeviceType());
    if (index < 0)
        return { "No audio system is available. Open Audio setup.", 0.0, false };
    auto* type = types[index];
    if (type == nullptr) return { "Could not open the audio system.", 0.0, false };
    type->scanForDevices();
    const auto inputs = type->getDeviceNames(true);
    const auto outputs = type->getDeviceNames(false);
    const auto savedInput=settings->getValue("inputDevice"),savedOutput=settings->getValue("outputDevice");
    logEvent("AUDIO enumerate "+names[index]+" inputs=["+inputs.joinIntoString(", ")+"] outputs=["+outputs.joinIntoString(", ")+"]");
    if(savedInput.isNotEmpty() && (!savedBackendAvailable||!inputs.contains(savedInput)||!outputs.contains(savedOutput)))
    {manager.closeAudioDevice();const auto msg="Saved audio device unavailable: "+settings->getValue("audioBackend")+" / "+savedInput+" -> "+savedOutput+". No alternative driver was opened. Open Audio setup.";logEvent("ERROR "+msg);return {msg,0,false};}
    const auto input = chooseDevice(inputs,savedInput,"Focusrite USB");
    const auto output = chooseDevice(outputs,savedOutput,"Focusrite USB");
    if (input < 0 || output < 0)
        return { "An input or output device is missing. Open Audio setup.", 0.0, false };
    auto setup = manager.getAudioDeviceSetup();
    setup.inputDeviceName = inputs[input];
    setup.outputDeviceName = outputs[output];
    setup.sampleRate = settings->getDoubleValue("sampleRate", 48000.0);
    setup.bufferSize = settings->getIntValue("bufferSize", 256);
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange(0, 2, true);
    setup.outputChannels.setRange(0, 2, true);
    juce::XmlElement xml("DEVICESETUP");xml.setAttribute("deviceType",names[index]);xml.setAttribute("audioInputDeviceName",setup.inputDeviceName);xml.setAttribute("audioOutputDeviceName",setup.outputDeviceName);
    xml.setAttribute("audioDeviceRate",setup.sampleRate);xml.setAttribute("audioDeviceBufferSize",setup.bufferSize);xml.setAttribute("audioDeviceInChans","11");xml.setAttribute("audioDeviceOutChans","11");
    logEvent("AUDIO opening exact request "+xml.toString());
    auto error = manager.initialise(2,2,&xml,false);
    if (error.isNotEmpty())
    {
        // Mono devices may reject a two-channel mask. JUCE opens the channels that
        // exist on most drivers, but retry channel 1 for those that require it.
        setup.inputChannels.clear();
        setup.inputChannels.setBit(0);
        logEvent("ERROR first open: "+error+"; retry same device with one input");
        xml.setAttribute("audioDeviceInChans","1");error=manager.initialise(1,2,&xml,false);
    }
    if (error.isNotEmpty()) {manager.closeAudioDevice();logEvent("ERROR audio open: "+error);return { "Could not open audio: " + error, 0.0, false };}
    const auto actual=currentAudioIdentity(manager);logEvent("AUDIO ready "+actual.backend+" | "+actual.input+" -> "+actual.output+" | "+juce::String(actual.sampleRate)+" Hz / "+juce::String(actual.bufferSize)+" samples | input mask="+actual.inputMask);
    return calibrationForActiveSetup(manager);
}

AudioSetupResult restoreAudioSetup(juce::AudioDeviceManager& manager)
{auto settings=openAudioPreferences();return restoreAudioSetupFrom(manager,settings.get());}

bool runPreferenceTests(juce::String& report)
{
    int checks = 0;
    juce::StringArray failures;
    const auto check = [&](bool condition, const juce::String& description)
    {
        ++checks;
        if (!condition) failures.add(description);
    };
    juce::StringArray devices { "Focusrite first", "My saved device", "Focusrite last" };
    struct FakeType final:juce::AudioIODeviceType
    {
        juce::StringArray requests;
        FakeType():AudioIODeviceType("Test ASIO"){}
        void scanForDevices()override{}
        juce::StringArray getDeviceNames(bool)const override{return {"Focusrite Thunderbolt ASIO","Focusrite USB ASIO"};}
        int getDefaultDeviceIndex(bool)const override{return 0;}
        int getIndexOfDevice(juce::AudioIODevice*,bool)const override{return -1;}
        bool hasSeparateInputsAndOutputs()const override{return false;}
        juce::AudioIODevice* createDevice(const juce::String& out,const juce::String& in)override{requests.add(out+" | "+in);return nullptr;}
    };
    juce::AudioDeviceManager fakeManager;auto fake=std::make_unique<FakeType>();auto* spy=fake.get();fakeManager.addAudioDeviceType(std::move(fake));
    juce::PropertySet desired;desired.setValue("audioBackend","Test ASIO");desired.setValue("inputDevice","Focusrite USB ASIO");desired.setValue("outputDevice","Focusrite USB ASIO");
    const auto failed=restoreAudioSetupFrom(fakeManager,&desired);
    check(spy->requests.size()==2 && spy->requests[0]=="Focusrite USB ASIO | Focusrite USB ASIO" && spy->requests[1]==spy->requests[0],"Exact USB open and same-device retry must never open default Thunderbolt driver.");
    check(failed.message.contains("Could not open audio")&&!failed.calibrated,"Open failure must remain visible and uncalibrated.");
    spy->requests.clear();desired.setValue("inputDevice","Missing saved device");restoreAudioSetupFrom(fakeManager,&desired);
    check(spy->requests.isEmpty(),"Missing saved device must not silently open another driver.");
    check(chooseDevice(devices, "My saved device", "Focusrite") == 1,
          "An exact saved device must beat every fallback match.");
    juce::PropertySet saved;
    const AudioDeviceIdentity active { "ASIO", "Focusrite USB ASIO", "Focusrite USB ASIO", "3", "3", 48000.0, 256 };
    saved.setValue("audioBackend", active.backend);
    saved.setValue("inputDevice", active.input);
    saved.setValue("outputDevice", active.output);
    saved.setValue("calibrationMs", 33.79166666666666);
    auto result = evaluateSavedCalibration(saved, active);
    check(result.calibrated && std::abs(result.compensationMs - 33.79166666666666) < 1.0e-9,
          "The user's legacy ASIO compensation must survive an exact device restore.");
    auto changed = active;
    changed.backend = "Windows Audio";
    check(!evaluateSavedCalibration(saved, changed).calibrated, "Legacy delay must not transfer to Windows Audio.");
    changed = active;
    changed.bufferSize = 512;
    check(!evaluateSavedCalibration(saved, changed).calibrated, "Legacy delay must not transfer to a different buffer.");
    saved.setValue("profileVersion", 2);
    saved.setValue("hasCalibration", true);
    saved.setValue("calibrationBackend", active.backend);
    saved.setValue("calibrationInput", active.input);
    saved.setValue("calibrationOutput", active.output);
    saved.setValue("calibrationInputMask", active.inputMask);
    saved.setValue("calibrationOutputMask", active.outputMask);
    saved.setValue("calibrationSampleRate", active.sampleRate);
    saved.setValue("calibrationBufferSize", active.bufferSize);
    check(evaluateSavedCalibration(saved, active).calibrated, "A complete matching profile must restore.");
    for (int field = 0; field < 7; ++field)
    {
        changed = active;
        switch (field)
        {
            case 0: changed.backend = "DirectSound"; break;
            case 1: changed.input = "Laptop mic"; break;
            case 2: changed.output = "Laptop speakers"; break;
            case 3: changed.inputMask = "1"; break;
            case 4: changed.outputMask = "1"; break;
            case 5: changed.sampleRate = 44100.0; break;
            case 6: changed.bufferSize = 512; break;
            default: break;
        }
        result = evaluateSavedCalibration(saved, changed);
        check(!result.calibrated && result.compensationMs == 0.0,
              "Mismatched profile field " + juce::String(field) + " must disable saved compensation.");
    }
    saved.setValue("hasCalibration", false);
    check(!evaluateSavedCalibration(saved, active).calibrated, "Saving devices only must not retain a stale correction.");
    saved.setValue("hasCalibration", true);
    saved.setValue("calibrationMs", -1.0);
    check(!evaluateSavedCalibration(saved, active).calibrated, "An invalid delay must not become compensation.");
    report = "Audio preferences: " + juce::String(checks - failures.size()) + "/" + juce::String(checks) + " passed.";
    if (!failures.isEmpty()) report += "\n" + failures.joinIntoString("\n");
    return failures.isEmpty();
}
}
