// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "SessionEngine.h"
#include "AudioPreferences.h"
#include "ProjectStore.h"
#include <future>

namespace studio
{
class TrackHeader;
class Timeline;
class StudioComponent final : public juce::Component, private juce::Timer
{
public:
    explicit StudioComponent(bool preview = false);
    ~StudioComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void requestClose();
    void handleUnexpectedError(const juce::String&);
    static bool runFeedbackUiChecks(juce::String&);

private:
    friend class TrackHeader;
    friend class Timeline;
    bool editable() const;
    void checkpoint();
    void changed(bool rebuild = false);
    bool save(bool showMessage = false);
    void restoreWorkspace();
    void newSession();
    void openSession();
    void addTrack();
    void deleteSelected();
    void splitSelected();
    void autoLevel();
    void checkMix();
    void undo(bool redo);
    void importFile();
    void exportFile();
    void openAudioSetup();
    void retryAudio();
    juce::String inputDescription(int) const;
    void refreshInputs();
    void beginPlay();
    void beginRecord();
    void stopTransport();
    void setPlayhead(double);
    void rebuildTracks();
    void timelineRepaint();
    void updateControls();
    void timerCallback() override;
    double viewDuration() const;
    int audioTop() const;
    double timeAt(float x) const;
    float xAt(double time) const;
    void message(const juce::String&);
    Clip* selectedClip();
    void loadPreview();

    juce::LookAndFeel_V4 look;
    SessionEngine engine;
    juce::AudioDeviceManager devices;
    AudioSetupResult audioSetup;
    juce::Viewport viewport;
    std::unique_ptr<Timeline> timeline;
    juce::ScrollBar scroll { false };
    std::vector<std::unique_ptr<TrackHeader>> trackHeaders;
    juce::TextButton newButton{"New"}, openButton{"Open"}, saveButton{"Save"}, exportButton{"Export WAV"}, settingsButton{"Audio setup"};
    juce::TextButton homeButton{"|<"}, playButton{"Play"}, stopButton{"Stop"}, recordButton{"Record"}, returnButton{"Return"};
    juce::TextButton addButton{"+ Track"}, importButton{"Import audio"}, splitButton{"Split"}, deleteButton{"Delete"}, undoButton{"Undo"}, redoButton{"Redo"}, levelButton{"Auto level"},mixCheckButton{"Mix check"};
    juce::ToggleButton clickButton{"Click"}, countButton{"Count in"};
    juce::Label name, clock, tempoLabel, latencyLabel, status, guide, zoomLabel,masterLabel;
    juce::Slider tempo, zoom,masterGain;
    juce::ComboBox tunerInput;
    juce::ComboBox defaultInputSelector;
    juce::TextButton diagnosticsButton{"Diagnostics"},retryButton{"Retry audio"},defaultTracksButton{"Tracks to default"};
    int defaultInput=0;bool applicationFault=false;
    juce::Label tunerTitle,tunerDisplay;
    std::vector<std::vector<LivePeak>> livePeaks;
    double pitchUpdated=0,recordViewStart=0;
    float pitchOpacity=0.0f;
    bool returnAfterRecording=false;
    PitchResult pitch;
    juce::File projectFile;
    std::unique_ptr<juce::PropertiesFile> workspace;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> audioWindow;
    std::future<juce::Result> exportTask;
    std::future<MixLevelAnalysis> mixAnalysisTask;
    std::vector<Session> history, future;
    juce::String selectedClipId;
    int selectedTrack = 0;
    double pixelsPerSecond = 32.0, viewStart = 0.0, lastRecordStart = 0.0;
    double displayedPosition = -1;
    bool pendingRebuild = false, dirty = false, previewMode = false, exporting = false, checkingMix=false,closing = false, chooserPending = false;
    juce::String observedError;
};
}
