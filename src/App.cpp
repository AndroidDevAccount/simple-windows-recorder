// SPDX-License-Identifier: AGPL-3.0-or-later
#include <JuceHeader.h>
#include "StudioComponent.h"
#include "AudioPreferences.h"

namespace studio { bool runProjectTests(juce::String&); }

class RecorderApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Simple Recorder"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String& args) override
    {
        if(args.contains("--self-test"))
        {
            juce::String a,b,c;
            const bool engine = studio::runEngineTests(a);
            const bool prefs = studio::runPreferenceTests(b);
            const bool projects = studio::runProjectTests(c);
            const auto report = a+"\n"+b+"\n"+c+"\n";
            juce::File::getCurrentWorkingDirectory().getChildFile("test-results.txt").replaceWithText(report);
            setApplicationReturnValue(engine && prefs && projects ? 0 : 1);
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
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { if(window)window->studio->requestClose();else quit(); }
    void anotherInstanceStarted(const juce::String&) override { if(window)window->toFront(true); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        studio::StudioComponent* studio=nullptr;
        explicit Window(bool preview) : DocumentWindow("Simple Recorder",juce::Colour(0xff10151e),allButtons)
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
