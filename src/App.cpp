// SPDX-License-Identifier: AGPL-3.0-or-later
#include <JuceHeader.h>
#include "StudioComponent.h"
#include "AudioPreferences.h"
#include "TrackEffects.h"
#include "Diagnostics.h"

namespace studio { bool runProjectTests(juce::String&); }

class RecorderApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Take One"; }
    const juce::String getApplicationVersion() override { return "0.7.0"; }
    bool moreThanOneInstanceAllowed() override
    { return getCommandLineParameters().contains("--self-test") || getCommandLineParameters().contains("--preview"); }
    void initialise(const juce::String& args) override
    {
        if(!args.contains("--self-test")&&!args.contains("--preview"))studio::startDiagnostics();
        try {initialiseImpl(args);}catch(const std::exception& e){studio::logEvent("FATAL startup: "+juce::String(e.what()));juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Take One startup failed",juce::String(e.what())+"\nSee diagnostic log: "+studio::diagnosticFile().getFullPathName(),"Close",nullptr,juce::ModalCallbackFunction::create([this](int){quit();}));}
    }
    void unhandledException(const std::exception* e,const juce::String& file,int line) override
    {const auto detail=(e?juce::String(e->what()):"Unknown exception")+" at "+file+":"+juce::String(line);studio::logEvent("ERROR unhandled UI exception: "+detail);if(window)window->studio->handleUnexpectedError(detail);else quit();}
    void initialiseImpl(const juce::String& args)
    {
        if(args.contains("--self-test"))
        {
            juce::String a,b,c,d,e,f;
            const bool engine = studio::runEngineTests(a);
            const bool prefs = studio::runPreferenceTests(b);
            const bool projects = studio::runProjectTests(c);
            const bool effects = studio::runEffectTests(d);
            const bool reverb = studio::runReverbTests(e);
            const bool feedback = studio::runFeedbackTests(f);
            const bool feedbackUi = studio::StudioComponent::runFeedbackUiChecks(f);
            const auto report = a+"\n"+b+"\n"+c+"\n"+d+"\n"+e+"\n"+f+"\n";
            juce::File::getCurrentWorkingDirectory().getChildFile("test-results.txt").replaceWithText(report);
            setApplicationReturnValue(engine && prefs && projects && effects && reverb && feedback && feedbackUi ? 0 : 1);
            quit(); return;
        }
        const bool preview=args.contains("--preview");
        window=std::make_unique<Window>(preview);
        if(preview && args.contains("--snapshot"))
        {
            juce::Timer::callAfterDelay(500,[this]
            {
                auto image=window->getContentComponent()->createComponentSnapshot(window->getContentComponent()->getLocalBounds(),true,1.0f);
                juce::File file=juce::File::getCurrentWorkingDirectory().getChildFile("studio-preview.png");
                juce::FileOutputStream stream(file);stream.setPosition(0);stream.truncate();juce::PNGImageFormat png;png.writeImageToStream(image,stream);
                quit();
            });
        }
    }
    void shutdown() override { window.reset();studio::logEvent("SHUTDOWN");juce::Logger::setCurrentLogger(nullptr); }
    void systemRequestedQuit() override { if(window)window->studio->requestClose();else quit(); }
    void anotherInstanceStarted(const juce::String&) override { if(window)window->toFront(true); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        studio::StudioComponent* studio=nullptr;
        explicit Window(bool preview) : DocumentWindow("Take One",juce::Colour(0xff10151e),allButtons)
        {
            setUsingNativeTitleBar(true);studio=new studio::StudioComponent(preview);setContentOwned(studio,true);
            setResizable(true,false);setResizeLimits(850,580,2000,1300);
            const auto area=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            centreWithSize(juce::jmin(1180,area.getWidth()-40),juce::jmin(740,area.getHeight()-70));
            setVisible(true);
        }
        void closeButtonPressed() override { studio->requestClose(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(RecorderApplication)
