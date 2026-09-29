// SPDX-License-Identifier: AGPL-3.0-or-later
#include "StudioComponent.h"
#include "CalibrationPanel.h"
#include "ProjectStore.h"
#include <algorithm>
#include <cmath>

namespace studio
{
namespace
{
constexpr int headerWidth = 242, rulerHeight = 38, trackHeight = 110;
const juce::Colour bg(0xff10151e), panel(0xff19212e), ink(0xffe8edf5), muted(0xff94a3b8), accent(0xff5ee0b5), red(0xffff657a);
juce::Colour trackColour(int i) { return std::array<juce::Colour, 4>{accent, juce::Colour(0xff78b9ff), juce::Colour(0xffffc778), juce::Colour(0xffc1a0ff)}[(size_t)i % 4]; }
juce::String formatTime(double time)
{
    const auto centis = (int64_t)std::llround(std::max(0.0, time) * 100);
    return juce::String::formatted("%02d:%02d.%02d", (int)(centis / 6000), (int)(centis / 100 % 60), (int)(centis % 100));
}
double parseTime(juce::String text)
{
    const auto parts = juce::StringArray::fromTokens(text, ":", "");
    if (parts.size() == 2) return std::max(0.0, parts[0].getDoubleValue() * 60 + parts[1].getDoubleValue());
    return std::max(0.0, text.getDoubleValue());
}
class SetupWindow final : public juce::DocumentWindow
{
public:
    std::function<void()> onClose;
    SetupWindow() : DocumentWindow("Audio setup and calibration", bg, closeButton)
    {
        auto* view = new juce::Viewport();
        auto* calibration = new CalibrationPanel();
        calibration->setSize(880, 850);
        view->setViewedComponent(calibration, true);
        view->setScrollBarsShown(true, true);
        setUsingNativeTitleBar(true);
        setContentOwned(view, false);
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        centreWithSize(juce::jmin(920, area.getWidth() - 60), juce::jmin(780, area.getHeight() - 80));
        setResizable(true, false);
        setVisible(true);
    }
    void closeButtonPressed() override { if (onClose) onClose(); }
};
}

class TrackHeader final : public juce::Component
{
public:
    TrackHeader(StudioComponent& studio, int index) : owner(studio), track(index)
    {
        auto& t = owner.engine.session().tracks[(size_t)track];
        title.setText(t.name, juce::dontSendNotification);
        title.setEditable(false, true);
        title.setColour(juce::Label::textColourId, ink);
        title.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        title.onTextChange = [this] { if (owner.editable()) { owner.checkpoint(); model().name = title.getText(); owner.changed(); } };
        arm.setButtonText("REC"); mute.setButtonText("M"); solo.setButtonText("S");
        for (auto* b : {&arm, &mute, &solo}) { b->setClickingTogglesState(true); addAndMakeVisible(b); }
        arm.setColour(juce::TextButton::buttonOnColourId, red.darker(0.4f));
        arm.setToggleState(t.armed, juce::dontSendNotification); mute.setToggleState(t.mute, juce::dontSendNotification); solo.setToggleState(t.solo, juce::dontSendNotification);
        arm.onClick = [this] { if (owner.editable()) { owner.checkpoint(); model().armed = arm.getToggleState(); owner.changed(); } };
        mute.onClick = [this] { if (owner.editable()) { owner.checkpoint(); model().mute = mute.getToggleState(); owner.changed(); } };
        solo.onClick = [this] { if (owner.editable()) { owner.checkpoint(); model().solo = solo.getToggleState(); owner.changed(); } };
        const int count = juce::jmax(2, owner.engine.inputChannelCount());
        for (int ch = 0; ch < count; ++ch) input.addItem("Input " + juce::String(ch + 1), ch + 1);
        input.setSelectedId(t.inputChannel + 1, juce::dontSendNotification);
        input.onChange = [this] { if (owner.editable()) { owner.checkpoint(); model().inputChannel = input.getSelectedId() - 1; owner.changed(); } };
        input.setTooltip("Scarlett Solo: Input 1 is the XLR microphone; Input 2 is the instrument jack.");
        gain.setSliderStyle(juce::Slider::LinearHorizontal); gain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        gain.setRange(0.0, 2.0, 0.01); gain.setValue(t.gain, juce::dontSendNotification);
        gain.setTooltip("Playback volume. Physical recording gain stays on the Scarlett.");
        gain.onDragStart = [this] { if (owner.editable()) owner.checkpoint(); };
        gain.onValueChange = [this] { if (owner.editable()) { model().gain = (float)gain.getValue(); owner.changed(); } };
        for (auto* c : std::initializer_list<juce::Component*>{&title, &input, &gain}) addAndMakeVisible(c);
    }
    Track& model() { return owner.engine.session().tracks[(size_t)track]; }
    void resized() override
    {
        title.setBounds(15, 7, 214, 26);
        arm.setBounds(17, 39, 45, 25); mute.setBounds(68, 39, 29, 25); solo.setBounds(102, 39, 29, 25);
        input.setBounds(137, 39, 90, 25); gain.setBounds(13, 73, 139, 22);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(owner.selectedTrack == track ? juce::Colour(0xff222f40) : panel);
        g.setColour(trackColour(track)); g.fillRect(0, 0, 3, getHeight() - 1);
        g.setColour(juce::Colour(0xff0c111a)); g.fillRoundedRectangle(164, 81, 63, 6, 3);
        const float peak = owner.engine.inputPeak(model().inputChannel);
        const float fraction = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(peak, -60.0f) + 60.0f) / 60.0f);
        g.setColour(peak >= 0.98f ? red : accent); g.fillRoundedRectangle(164, 81, 63 * fraction, 6, 3);
        g.setColour(muted); g.setFont(10.0f); g.drawText("IN", 163, 89, 25, 13, juce::Justification::left);
        g.setColour(bg); g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }
    void mouseDown(const juce::MouseEvent&) override { owner.selectedTrack = track; owner.selectedClipId.clear(); owner.timelineRepaint(); }
    void update() { const bool can = owner.editable(); for (auto* c : std::initializer_list<juce::Component*>{&title,&arm,&mute,&solo,&input,&gain}) c->setEnabled(can); repaint(); }
private:
    StudioComponent& owner;
    int track;
    juce::Label title;
    juce::TextButton arm, mute, solo;
    juce::ComboBox input;
    juce::Slider gain;
};

class Timeline final : public juce::Component
{
public:
    explicit Timeline(StudioComponent& s) : owner(s) { setMouseCursor(juce::MouseCursor::CrosshairCursor); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(bg);
        auto& session = owner.engine.session();
        g.setColour(panel); g.fillRect(0, 0, getWidth(), rulerHeight);
        g.setColour(muted); g.setFont(11.0f); g.drawText("TRACK / INPUT", 18, 0, headerWidth - 24, rulerHeight, juce::Justification::centredLeft);
        const double step = owner.pixelsPerSecond < 16 ? 10 : (owner.pixelsPerSecond < 60 ? 5 : 1);
        for (double t = std::ceil(owner.viewStart / step) * step; t < owner.viewStart + owner.viewDuration(); t += step)
        {
            const float x = owner.xAt(t);
            g.setColour(juce::Colour(0xff263242)); g.drawVerticalLine((int)x, rulerHeight, (float)getHeight());
            g.setColour(muted); g.drawText(formatTime(t).dropLastCharacters(3), (int)x + 5, 3, 66, 30, juce::Justification::left);
        }
        for (int index = 0; index < (int)session.tracks.size(); ++index)
        {
            const auto& track = session.tracks[(size_t)index];
            const int y = rulerHeight + index * trackHeight;
            if (index % 2 == 0) { g.setColour(juce::Colours::white.withAlpha(0.015f)); g.fillRect(headerWidth, y, getWidth() - headerWidth, trackHeight); }
            g.setColour(panel); g.drawHorizontalLine(y + trackHeight - 1, (float)headerWidth, (float)getWidth());
            g.saveState(); g.reduceClipRegion(headerWidth, y, getWidth() - headerWidth, trackHeight);
            if (track.clips.empty())
            {
                g.setColour(muted.withAlpha(0.45f)); g.setFont(13.0f);
                g.drawText(track.armed ? "Ready to record from the playhead" : "Arm REC to record here", headerWidth + 20, y, 310, trackHeight, juce::Justification::centredLeft);
            }
            for (const auto& clip : track.clips)
            {
                const auto x = owner.xAt(clip.startSeconds);
                const auto w = (float)(clip.lengthSeconds * owner.pixelsPerSecond);
                if (x + w < headerWidth || x > getWidth()) continue;
                juce::Rectangle<float> rect(x, (float)y + 8, std::max(2.0f, w), (float)trackHeight - 16);
                const auto colour = trackColour(index);
                g.setColour(colour.withAlpha(track.mute ? 0.08f : 0.17f)); g.fillRoundedRectangle(rect, 5.0f);
                g.setColour(clip.id == owner.selectedClipId ? ink : colour.withAlpha(0.65f)); g.drawRoundedRectangle(rect, 5, clip.id == owner.selectedClipId ? 2.0f : 1.0f);
                g.setColour(colour); g.setFont(11.0f); g.drawText("TAKE  " + formatTime(clip.startSeconds), rect.toNearestInt().reduced(8).removeFromTop(17), juce::Justification::left, true);
                if (!clip.audio) continue;
                const float centre = y + 63.0f;
                const int left = std::max(headerWidth, (int)x + 2), right = std::min(getWidth(), (int)(x + w) - 2);
                for (int px = left; px < right; ++px)
                {
                    double t0 = std::max(0.0, owner.timeAt((float)px) - clip.startSeconds) + clip.sourceOffsetSeconds;
                    double t1 = std::min(clip.lengthSeconds, owner.timeAt((float)px + 1) - clip.startSeconds) + clip.sourceOffsetSeconds;
                    int a = juce::jlimit(0, clip.audio->getNumSamples(), (int)(t0 * clip.sampleRate));
                    int b = juce::jlimit(a, clip.audio->getNumSamples(), (int)std::ceil(t1 * clip.sampleRate));
                    float min = 0, max = 0;
                    const auto* data = clip.audio->getReadPointer(0);
                    for (int n = a; n < b; ++n) { min = std::min(min, data[n]); max = std::max(max, data[n]); }
                    g.drawVerticalLine(px, centre - juce::jlimit(0.0f, 1.0f, max * (float)clip.gain) * 24,
                                          centre - juce::jlimit(-1.0f, 0.0f, min * (float)clip.gain) * 24 + 0.6f);
                }
            }
            if (owner.engine.isRecording() && track.armed && !owner.engine.isCountingIn())
            {
                const float x = owner.xAt(owner.engine.recordingStart());
                const float w = (float)std::max(2.0, (owner.engine.position() - owner.engine.recordingStart()) * owner.pixelsPerSecond);
                g.setColour(red.withAlpha(0.23f)); g.fillRoundedRectangle(x, (float)y + 8, w, trackHeight - 16.0f, 5);
                g.setColour(red); g.drawText("RECORDING", (int)x + 10, y + 13, 120, 20, juce::Justification::left);
            }
            g.restoreState();
        }
        const float cursor = owner.xAt(owner.engine.position());
        if (cursor >= headerWidth && cursor < getWidth())
        {
            g.setColour(owner.engine.isRecording() ? red : ink); g.drawLine(cursor, 24, cursor, (float)getHeight(), 1.4f);
            juce::Path p; p.addTriangle(cursor-5, 18, cursor+5, 18, cursor, 26); g.fillPath(p);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!owner.editable() || e.x < headerWidth) return;
        drag = false; mode = 0;
        owner.selectedClipId.clear();
        const int index = (e.y - rulerHeight) / trackHeight;
        const auto time = owner.timeAt(e.position.x);
        if (e.y >= rulerHeight && juce::isPositiveAndBelow(index, (int)owner.engine.session().tracks.size()))
        {
            owner.selectedTrack = index;
            for (auto& clip : owner.engine.session().tracks[(size_t)index].clips)
            {
                if (time >= clip.startSeconds && time < clip.startSeconds + clip.lengthSeconds)
                {
                    owner.selectedClipId = clip.id; original = clip;
                    mode = std::abs(e.x - owner.xAt(clip.startSeconds)) < 7 ? 1 :
                        (std::abs(e.x - owner.xAt(clip.startSeconds + clip.lengthSeconds)) < 7 ? 2 : 3);
                    break;
                }
            }
        }
        owner.setPlayhead(time); owner.updateControls(); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!owner.editable() || mode == 0 || e.getDistanceFromDragStart() < 5) return;
        auto* clip = owner.selectedClip(); if (!clip) return;
        if (!drag) { owner.checkpoint(); drag = true; }
        const double delta = e.getDistanceFromDragStartX() / owner.pixelsPerSecond;
        if (mode == 3) clip->startSeconds = std::max(0.0, original.startSeconds + delta);
        if (mode == 1)
        {
            const double d = juce::jlimit(-std::min(original.sourceOffsetSeconds, original.startSeconds), original.lengthSeconds - 0.01, delta);
            clip->startSeconds = original.startSeconds + d; clip->sourceOffsetSeconds = original.sourceOffsetSeconds + d; clip->lengthSeconds = original.lengthSeconds - d;
        }
        if (mode == 2)
        {
            const double available = original.audio ? original.audio->getNumSamples() / original.sampleRate - original.sourceOffsetSeconds : original.lengthSeconds;
            clip->lengthSeconds = juce::jlimit(0.01, std::max(0.01, available), original.lengthSeconds + delta);
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (!drag) return;
        // A moved/trimmed clip wins its span, just like a punch. Old sources stay on disk.
        if (auto* clip = owner.selectedClip())
        {
            const auto replacement = *clip;
            auto& track = owner.engine.session().tracks[(size_t)owner.selectedTrack];
            std::erase_if(track.clips, [&](const auto& c) { return c.id == replacement.id; });
            SessionEngine::insertPunch(track, replacement);
        }
        owner.changed(); drag = false;
    }
private:
    StudioComponent& owner;
    Clip original;
    bool drag = false;
    int mode = 0;
};

StudioComponent::StudioComponent(bool preview) : previewMode(preview)
{
    look.setColour(juce::ResizableWindow::backgroundColourId, bg);
    look.setColour(juce::TextButton::buttonColourId, panel.brighter(0.06f));
    look.setColour(juce::TextButton::buttonOnColourId, accent.darker(0.7f));
    look.setColour(juce::TextButton::textColourOffId, ink);
    look.setColour(juce::ComboBox::backgroundColourId, bg);
    look.setColour(juce::ComboBox::textColourId, ink);
    look.setColour(juce::Label::textColourId, ink);
    look.setColour(juce::Slider::thumbColourId, accent);
    look.setColour(juce::Slider::trackColourId, accent.darker(0.4f));
    look.setColour(juce::ToggleButton::textColourId, muted);
    setLookAndFeel(&look); setWantsKeyboardFocus(true);
    for (auto* c : std::initializer_list<juce::Component*>{&name,&clock,&tempoLabel,&latencyLabel,&status,&guide,&zoomLabel,&tempo,&zoom,&clickButton,&countButton,&viewport,&scroll}) addAndMakeVisible(c);
    for (auto* b : {&newButton,&openButton,&saveButton,&exportButton,&settingsButton,&homeButton,&playButton,&stopButton,&recordButton,&returnButton,&addButton,&importButton,&splitButton,&deleteButton,&undoButton,&redoButton,&levelButton})
    { addAndMakeVisible(b); b->setWantsKeyboardFocus(false); }
    recordButton.setColour(juce::TextButton::buttonColourId, red.darker(0.35f));
    playButton.setColour(juce::TextButton::buttonColourId, accent.darker(0.65f));
    name.setFont(juce::FontOptions(22.0f, juce::Font::bold)); name.setEditable(false, true);
    name.onTextChange = [this] { if(editable()) { engine.session().name = name.getText(); changed(); } };
    clock.setFont(juce::FontOptions("Consolas", 23.0f, juce::Font::bold)); clock.setEditable(false, true);
    clock.setTooltip("Double-click to set a position, for example 0:30 or 30 seconds.");
    clock.onTextChange = [this] { if(editable()) setPlayhead(parseTime(clock.getText())); };
    guide.setText("Arm a track. Place the playhead. Record a take.", juce::dontSendNotification); guide.setColour(juce::Label::textColourId, muted);
    tempoLabel.setText("BPM", juce::dontSendNotification); zoomLabel.setText("Zoom", juce::dontSendNotification);
    tempo.setRange(40,240,1); tempo.setSliderStyle(juce::Slider::IncDecButtons); tempo.setTextBoxStyle(juce::Slider::TextBoxLeft,false,42,24);
    tempo.onValueChange = [this] { if(editable()) { engine.session().bpm = tempo.getValue(); changed(); } };
    zoom.setRange(8,180,1); zoom.setValue(pixelsPerSecond); zoom.setSliderStyle(juce::Slider::LinearHorizontal); zoom.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
    zoom.onValueChange = [this] { pixelsPerSecond = zoom.getValue(); resized(); };
    clickButton.onClick = [this] { if(editable()) { engine.session().metronome = clickButton.getToggleState(); changed(); } };
    countButton.onClick = [this] { if(editable()) { engine.session().countInBars = countButton.getToggleState() ? 1 : 0; changed(); } };
    newButton.onClick=[this]{newSession();}; openButton.onClick=[this]{openSession();}; saveButton.onClick=[this]{save(true);}; exportButton.onClick=[this]{exportFile();}; settingsButton.onClick=[this]{openAudioSetup();};
    homeButton.onClick=[this]{setPlayhead(0); viewStart=0; resized();}; returnButton.onClick=[this]{setPlayhead(lastRecordStart);};
    playButton.onClick=[this]{beginPlay();}; stopButton.onClick=[this]{stopTransport();}; recordButton.onClick=[this]{beginRecord();};
    addButton.onClick=[this]{addTrack();}; importButton.onClick=[this]{importFile();}; splitButton.onClick=[this]{splitSelected();}; deleteButton.onClick=[this]{deleteSelected();};
    undoButton.onClick=[this]{undo(false);}; redoButton.onClick=[this]{undo(true);}; levelButton.onClick=[this]{autoLevel();};
    timeline = std::make_unique<Timeline>(*this); viewport.setViewedComponent(timeline.get(), false); viewport.setScrollBarsShown(true, false);
    // The UI timer reads the horizontal scrollbar without involving the audio callback.
    juce::PropertiesFile::Options options; options.applicationName="Workspace"; options.filenameSuffix=".settings"; options.folderName="SimpleWindowsRecorder"; options.osxLibrarySubFolder="Application Support";
    workspace=std::make_unique<juce::PropertiesFile>(options);
    setSize(1180,740);
    if (previewMode) loadPreview();
    else { restoreWorkspace(); audioSetup=restoreAudioSetup(devices); devices.addAudioCallback(&engine); }
    rebuildTracks(); setSize(1180,740); startTimerHz(30);
    message(previewMode ? "Preview session - no audio devices opened" : audioSetup.message);
}

StudioComponent::~StudioComponent()
{
    stopTimer(); audioWindow.reset();
    devices.removeAudioCallback(&engine); devices.closeAudioDevice();
    if (!previewMode && !engine.isBusy()) save();
    viewport.setViewedComponent(nullptr, false); trackHeaders.clear(); timeline.reset();
    setLookAndFeel(nullptr);
}
bool StudioComponent::editable() const { return !engine.isBusy() && !exporting && !audioWindow && !chooserPending; }
void StudioComponent::message(const juce::String& s) { status.setText(s, juce::dontSendNotification); }
void StudioComponent::timelineRepaint() { timeline->repaint(); for(auto& h:trackHeaders)h->repaint(); }
void StudioComponent::checkpoint() { history.push_back(engine.session()); if(history.size()>30) history.erase(history.begin()); future.clear(); }
void StudioComponent::changed(bool rebuild) { dirty=true; pendingRebuild |= rebuild; updateControls(); timeline->repaint(); }
bool StudioComponent::save(bool show)
{
    if (previewMode || projectFile == juce::File()) return true;
    if (engine.isBusy()) return false;
    const auto result=saveProject(engine.session(),projectFile,engine.position());
    if(result.failed()) message(result.getErrorMessage());
    else { dirty=false; if(show) message("Saved: " + projectFile.getFullPathName()); workspace->setValue("lastProject",projectFile.getFullPathName()); workspace->saveIfNeeded(); }
    return result.wasOk();
}
void StudioComponent::restoreWorkspace()
{
    const juce::File previous(workspace->getValue("lastProject"));
    if(previous.existsAsFile())
    {
        double position=0; const auto r=loadProject(engine.session(),previous,position);
        if(r.wasOk()) { projectFile=previous; engine.seek(position); return; }
        // Preserve a project that could not load; the new session is in a different directory.
    }
    newSession();
}
void StudioComponent::newSession()
{
    if(!editable()) return;
    if(!save())return; history.clear(); future.clear(); selectedClipId.clear(); selectedTrack=0;
    engine.session()=Session{};
    for(int i=0;i<3;++i) { Track t; t.id=juce::Uuid().toString(); t.name=juce::StringArray{"Voice","Guitar","Bass"}[i]; t.armed=i==0; t.inputChannel=i==0?0:1; engine.session().tracks.push_back(t); }
    const auto folder=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Simple Recorder Sessions")
        .getNonexistentChildFile("Song " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M"), "");
    projectFile=folder.getChildFile("Song.srproject"); engine.seek(0); viewStart=0;
    if(timeline) { rebuildTracks(); save(); message("New session. Voice is armed on Input 1; guitar uses Input 2."); }
}
void StudioComponent::openSession()
{
    if(!editable() || !save()) return;
    chooserPending=true;
    chooser=std::make_unique<juce::FileChooser>("Open a Simple Recorder project", projectFile.getParentDirectory(), "*.srproject");
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<StudioComponent>(this)](const auto& c)
    {
        if(!safe) return; safe->chooserPending=false;
        if(!safe->editable() || c.getResult()==juce::File()) return;
        Session next; double time=0; const auto r=loadProject(next,c.getResult(),time);
        if(r.failed()) { safe->message(r.getErrorMessage()); return; }
        safe->engine.session()=std::move(next); safe->projectFile=c.getResult(); safe->engine.seek(time); safe->history.clear(); safe->future.clear(); safe->selectedClipId.clear(); safe->selectedTrack=0; safe->viewStart=0; safe->rebuildTracks(); safe->save(); safe->message("Session opened. All source takes are preserved.");
    });
}
void StudioComponent::addTrack()
{
    if(!editable()) return; checkpoint(); Track t; t.id=juce::Uuid().toString(); t.name="Track " + juce::String(engine.session().tracks.size()+1); engine.session().tracks.push_back(t); selectedTrack=(int)engine.session().tracks.size()-1; changed(true);
}
Clip* StudioComponent::selectedClip()
{
    if(!juce::isPositiveAndBelow(selectedTrack,(int)engine.session().tracks.size())) return nullptr;
    for(auto& c:engine.session().tracks[(size_t)selectedTrack].clips) if(c.id==selectedClipId) return &c;
    return nullptr;
}
void StudioComponent::deleteSelected()
{
    if(!editable() || !selectedClip()) return; checkpoint(); auto& clips=engine.session().tracks[(size_t)selectedTrack].clips;
    std::erase_if(clips,[this](auto& c){return c.id==selectedClipId;}); selectedClipId.clear(); changed(); message("Clip removed from the timeline. Undo restores it; its source file is retained.");
}
void StudioComponent::splitSelected()
{
    if(!editable()) return; auto* c=selectedClip(); if(!c) {message("Select a clip and place the playhead inside it to split.");return;}
    const double delta=engine.position()-c->startSeconds;
    if(delta<=0.001 || delta>=c->lengthSeconds-0.001) return;
    checkpoint(); Clip right=*c; right.id=juce::Uuid().toString(); right.startSeconds+=delta; right.sourceOffsetSeconds+=delta; right.lengthSeconds-=delta; c->lengthSeconds=delta;
    engine.session().tracks[(size_t)selectedTrack].clips.push_back(right); changed();
}
void StudioComponent::autoLevel()
{
    if(!editable()) return; auto* c=selectedClip(); if(!c || !c->audio) return; checkpoint();
    const int first=juce::jlimit(0,c->audio->getNumSamples(),(int)(c->sourceOffsetSeconds*c->sampleRate));
    const int n=juce::jlimit(0,c->audio->getNumSamples()-first,(int)(c->lengthSeconds*c->sampleRate));
    const auto peak=c->audio->getMagnitude(first,n); c->gain=peak>0.0001f?juce::jlimit(0.1,16.0,0.5/(double)peak):1.0; changed(); message("Clip playback level adjusted. Original recording is unchanged.");
}
void StudioComponent::undo(bool redo)
{
    if(!editable()) return; auto& from=redo?future:history; auto& to=redo?history:future; if(from.empty()) return;
    to.push_back(engine.session()); engine.session()=std::move(from.back()); from.pop_back(); selectedClipId.clear(); selectedTrack=juce::jlimit(0,juce::jmax(0,(int)engine.session().tracks.size()-1),selectedTrack); changed(true); message(redo?"Edit restored.":"Undone. The previous take is back.");
}
void StudioComponent::importFile()
{
    if(!editable() || engine.session().tracks.empty()) return;
    chooserPending=true;
    chooser=std::make_unique<juce::FileChooser>("Import audio to selected track",juce::File(),"*.wav;*.aiff;*.flac;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<StudioComponent>(this)](const auto& c)
    { if(!safe)return;safe->chooserPending=false;if(!safe->editable() || c.getResult()==juce::File())return; Clip clip; auto r=importAudio(clip,c.getResult(),safe->projectFile.getSiblingFile("Media")); if(r.failed()){safe->message(r.getErrorMessage());return;} safe->checkpoint(); clip.startSeconds=safe->engine.position(); SessionEngine::insertPunch(safe->engine.session().tracks[(size_t)safe->selectedTrack],clip); safe->selectedClipId=clip.id; safe->changed(); safe->message("Imported audio at the playhead."); });
}
void StudioComponent::exportFile()
{
    if(!editable()) return;
    chooserPending=true;
    chooser=std::make_unique<juce::FileChooser>("Export stereo WAV",projectFile.getSiblingFile("Mix.wav"),"*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<StudioComponent>(this)](const auto& c)
    { if(!safe)return;safe->chooserPending=false;if(!safe->editable() || c.getResult()==juce::File())return; safe->exporting=true; safe->message("Exporting stereo WAV..."); safe->exportTask=std::async(std::launch::async,[session=safe->engine.session(),file=c.getResult()]{return exportMix(session,file,48000.0);}); });
}
void StudioComponent::openAudioSetup()
{
    if(!editable()) return; save(); devices.removeAudioCallback(&engine); devices.closeAudioDevice();
    auto w=std::make_unique<SetupWindow>();
    w->onClose=[safe=juce::Component::SafePointer<StudioComponent>(this)]
    { juce::MessageManager::callAsync([safe]{if(!safe)return; safe->audioWindow.reset(); safe->audioSetup=restoreAudioSetup(safe->devices); safe->devices.addAudioCallback(&safe->engine); safe->rebuildTracks(); safe->message(safe->audioSetup.message);}); };
    audioWindow=std::move(w);
}
void StudioComponent::beginPlay()
{
    if(!editable()) return; if(!engine.play(engine.position())) message(engine.lastError()); else message("Playing. Space to stop."); updateControls();
}
void StudioComponent::beginRecord()
{
    if(!editable()) return; checkpoint(); lastRecordStart=engine.position();
    audioSetup=calibrationForActiveSetup(devices);
    double correction=audioSetup.compensationMs;
    if(!audioSetup.calibrated)
        if(auto* d=devices.getCurrentAudioDevice()) correction=1000.0*(d->getInputLatencyInSamples()+d->getOutputLatencyInSamples())/d->getCurrentSampleRate();
    if(!engine.record(lastRecordStart,correction,projectFile.getSiblingFile("Media"))) {history.pop_back();message(engine.lastError());}
    else message("Recording from " + formatTime(lastRecordStart) + ". Stop finishes the punch; Return goes back for another take.");
    updateControls();
}
void StudioComponent::stopTransport() { engine.stop(); updateControls(); }
void StudioComponent::setPlayhead(double value)
{
    if(!editable()) return;
    value=std::max(0.0,value); engine.seek(value);
    if(value<viewStart || value>viewStart+viewDuration()*0.95)
    { viewStart=std::max(0.0,value-viewDuration()*0.2); resized(); }
    displayedPosition=-1; dirty=true; timeline->repaint();
}
double StudioComponent::viewDuration() const { return std::max(1.0,(viewport.getWidth()-headerWidth-16)/pixelsPerSecond); }
double StudioComponent::timeAt(float x) const { return std::max(0.0,viewStart+(x-headerWidth)/pixelsPerSecond); }
float StudioComponent::xAt(double t) const { return (float)(headerWidth+(t-viewStart)*pixelsPerSecond); }
void StudioComponent::rebuildTracks()
{
    trackHeaders.clear();
    for(int i=0;i<(int)engine.session().tracks.size();++i) {auto h=std::make_unique<TrackHeader>(*this,i); timeline->addAndMakeVisible(*h); trackHeaders.push_back(std::move(h));}
    name.setText(engine.session().name,juce::dontSendNotification); tempo.setValue(engine.session().bpm,juce::dontSendNotification); clickButton.setToggleState(engine.session().metronome,juce::dontSendNotification); countButton.setToggleState(engine.session().countInBars>0,juce::dontSendNotification); resized(); updateControls();
}
void StudioComponent::updateControls()
{
    const bool can=editable();
    for(auto* c:std::initializer_list<juce::Component*>{&newButton,&openButton,&saveButton,&exportButton,&settingsButton,&homeButton,&returnButton,&playButton,&recordButton,&addButton,&importButton,&splitButton,&deleteButton,&levelButton,&tempo,&clickButton,&countButton,&name,&clock}) c->setEnabled(can);
    stopButton.setEnabled(engine.isBusy()); undoButton.setEnabled(can&&!history.empty()); redoButton.setEnabled(can&&!future.empty());
    for(auto& h:trackHeaders) h->update();
    latencyLabel.setText(audioSetup.calibrated?juce::String(audioSetup.compensationMs,1)+" ms correction":"Driver timing - calibrate in Audio setup",juce::dontSendNotification);
}
void StudioComponent::timerCallback()
{
    if(engine.poll()) {dirty=true;pendingRebuild=true;message("Take saved. Punch-in replaced only the recorded span. Undo restores the previous take.");}
    const auto error=engine.lastError(); if(error.isNotEmpty() && error!=observedError) {observedError=error;message(error);}
    if(exporting && exportTask.valid() && exportTask.wait_for(std::chrono::seconds(0))==std::future_status::ready) {auto r=exportTask.get();exporting=false;message(r.wasOk()?"Stereo WAV exported.":r.getErrorMessage());}
    if(pendingRebuild&&!engine.isBusy()) {pendingRebuild=false;rebuildTracks();}
    if(dirty && editable()) save();
    const double p=engine.position();
    if(!clock.isBeingEdited() && p!=displayedPosition) {clock.setText(formatTime(p),juce::dontSendNotification);displayedPosition=p;}
    if(engine.isCountingIn()) message("Count in: " + juce::String(engine.countInBeatsRemaining()) + " beats. Recording will begin at " + formatTime(lastRecordStart));
    else if(engine.isRecording()) message("Recording. Stop finishes the punch; Return goes back for another take.");
    if(engine.isBusy() && p>viewStart+viewDuration()*0.95) {viewStart=std::max(0.0,p-viewDuration()*0.2);resized();}
    else if(std::abs(scroll.getCurrentRangeStart()-viewStart)>0.001) {viewStart=scroll.getCurrentRangeStart();}
    updateControls(); timeline->repaint();
    if(closing && !engine.isBusy() && !exporting) {if(save())juce::JUCEApplication::getInstance()->quit();else closing=false;}
}
bool StudioComponent::keyPressed(const juce::KeyPress& k)
{
    if(k.getKeyCode()==juce::KeyPress::spaceKey) {if(engine.isBusy()) stopTransport(); else beginPlay();return true;}
    if(k.getModifiers().isCommandDown()&&k.getTextCharacter()=='z') {undo(k.getModifiers().isShiftDown());return true;}
    if(k.getModifiers().isCommandDown()&&k.getTextCharacter()=='s') {save(true);return true;}
    if(k.getTextCharacter()=='r'||k.getTextCharacter()=='R') {beginRecord();return true;}
    if(k.getKeyCode()==juce::KeyPress::deleteKey||k.getKeyCode()==juce::KeyPress::backspaceKey) {deleteSelected();return true;}
    if(k.getKeyCode()==juce::KeyPress::returnKey) {setPlayhead(lastRecordStart);return true;}
    return false;
}
void StudioComponent::requestClose() {closing=true;if(engine.isBusy())engine.stop();else if(!exporting){if(save())juce::JUCEApplication::getInstance()->quit();else closing=false;}}
void StudioComponent::paint(juce::Graphics& g)
{
    g.fillAll(bg);g.setColour(panel);g.fillRect(0,68,getWidth(),76);g.setColour(accent);g.fillRoundedRectangle(20,22,5,28,2);
    g.setColour(muted);g.setFont(11.0f);g.drawText("PUNCH-IN RECORDER",32,getHeight()-28,160,18,juce::Justification::left);
    g.drawText("Space play/stop   R record   Enter return   Ctrl+Z undo",198,getHeight()-28,getWidth()-218,18,juce::Justification::right);
}
void StudioComponent::resized()
{
    const int w=getWidth(),h=getHeight();
    name.setBounds(32,14,std::max(180,w-570),32); guide.setBounds(32,44,w-300,20);
    int x=w-512;
    for(auto* b:{&newButton,&openButton,&saveButton}){b->setBounds(x,22,58,28);x+=64;}
    exportButton.setBounds(x,22,102,28);x+=110;settingsButton.setBounds(x,22,102,28);
    homeButton.setBounds(18,88,38,34);playButton.setBounds(62,88,62,34);stopButton.setBounds(130,88,62,34);recordButton.setBounds(198,88,76,34);returnButton.setBounds(280,88,62,34);
    clock.setBounds(354,85,140,42);
    tempoLabel.setBounds(502,76,44,18);tempo.setBounds(502,97,82,26); clickButton.setBounds(596,80,62,24);countButton.setBounds(596,107,90,24);
    latencyLabel.setBounds(694,79,std::max(80,w-710),52);latencyLabel.setFont(12.0f);latencyLabel.setColour(juce::Label::textColourId,muted);
    x=18;for(auto* b:{&addButton,&importButton,&undoButton,&redoButton,&splitButton,&deleteButton,&levelButton}){const int width=b==&importButton?104:(b==&levelButton?84:66);b->setBounds(x,157,width,27);x+=width+6;}
    zoomLabel.setBounds(w-190,157,40,27);zoom.setBounds(w-150,157,130,27);
    viewport.setBounds(16,198,w-32,std::max(100,h-291));
    timeline->setSize(viewport.getWidth()-16,std::max(viewport.getHeight(),rulerHeight+(int)trackHeaders.size()*trackHeight+20));
    for(int i=0;i<(int)trackHeaders.size();++i)trackHeaders[(size_t)i]->setBounds(0,rulerHeight+i*trackHeight,headerWidth,trackHeight);
    scroll.setBounds(16+headerWidth,h-86,w-32-headerWidth,12); scroll.setRangeLimits(0,std::max({120.0,engine.duration()+30,viewStart+viewDuration()}));scroll.setCurrentRange(viewStart,viewDuration(),juce::dontSendNotification);
    status.setBounds(20,h-65,w-40,30);status.setFont(12.0f);status.setColour(juce::Label::textColourId,muted);
}
void StudioComponent::loadPreview()
{
    engine.session()=Session{};engine.session().name="Evening ideas";
    for(int i=0;i<4;++i)
    {
        Track t;t.id=juce::Uuid().toString();t.name=juce::StringArray{"Voice","Acoustic guitar","Bass","Harmony"}[i];t.armed=i==0;t.inputChannel=i==0?0:1;
        if(i<3){Clip c;c.id=juce::Uuid().toString();c.startSeconds=i==0?7:0;c.lengthSeconds=i==0?15:37;c.sampleRate=48000;c.audio=std::make_shared<juce::AudioBuffer<float>>(1,(int)(c.lengthSeconds*48000));for(int n=0;n<c.audio->getNumSamples();++n){const double time=n/48000.0;const double env=std::pow(std::max(0.0,std::sin(time*(i==0?3:6))),i==0?0.5:3.0);c.audio->setSample(0,n,(float)(0.5*env*std::sin(time*juce::MathConstants<double>::twoPi*(110+55*i))));}t.clips.push_back(c);}
        engine.session().tracks.push_back(t);
    }
    engine.seek(30);viewStart=10;audioSetup.calibrated=true;audioSetup.compensationMs=33.8;
}
}
