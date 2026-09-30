// SPDX-License-Identifier: AGPL-3.0-or-later
#include "StudioComponent.h"
#include "CalibrationPanel.h"
#include "ProjectStore.h"
#include "TrackEffects.h"
#include "Diagnostics.h"
#include <algorithm>
#include <cmath>

namespace studio
{
namespace
{
constexpr int headerWidth = 242, rulerHeight = 38, trackHeight = 233;
constexpr double tunerHoldMs = 1800.0, tunerFadeStartMs = 1200.0;
const juce::Colour bg(0xff10151e), panel(0xff19212e), ink(0xffe8edf5), muted(0xff94a3b8), accent(0xff5ee0b5), red(0xffff657a);
float waveformSample(float raw,const Clip& clip,const Track& track) noexcept{return raw*(float)clip.gain*track.gain;}
float waveformDisplay(float sample) noexcept{const float magnitude=std::abs(sample);const float shown=magnitude<=1.0f?std::sqrt(magnitude):1.0f+std::min(0.28f,(magnitude-1.0f)*0.25f);return std::copysign(shown,sample);}
juce::String inputRating(double peakDb){return peakDb>=-0.5?"CLIPPING":peakDb>-3?"HOT":peakDb>=-18?"HEALTHY":peakDb>=-24?"SAFE / QUIET":"QUIET";}
void updateHeldPitch(PitchResult& displayed,double& lastValid,float& opacity,const PitchResult* measured,double now,bool audioAvailable)
{
    if(!audioAvailable){displayed={};lastValid=0;opacity=0;return;}
    if(measured!=nullptr&&measured->midi>=0){displayed=*measured;lastValid=now;opacity=1;return;}
    const auto age=now-lastValid;
    if(displayed.midi<0||lastValid<=0||age>tunerHoldMs){displayed={};opacity=0;return;}
    opacity=(float)juce::jmap(juce::jlimit(tunerFadeStartMs,tunerHoldMs,age),tunerFadeStartMs,tunerHoldMs,1.0,0.28);
}
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
        input.addItem("Default: "+owner.inputDescription(owner.defaultInput),1);
        for (int ch = 0; ch < count; ++ch) input.addItem(owner.inputDescription(ch), ch + 2);
        input.setSelectedId(t.inputChannel + 2, juce::dontSendNotification);
        input.onChange = [this] { if (owner.editable()) { owner.checkpoint(); model().inputChannel = input.getSelectedId() - 2; owner.changed(true); } };
        input.setTooltip("Recording source: "+owner.inputDescription(t.inputChannel<0?owner.defaultInput:t.inputChannel)+". Default follows the saved input at the top of the window.");
        gainLabel.setText("Gain",juce::dontSendNotification);gainLabel.setFont(juce::FontOptions(11.0f,juce::Font::bold));gainLabel.setColour(juce::Label::textColourId,muted);
        gain.setSliderStyle(juce::Slider::LinearHorizontal);gain.setTextBoxStyle(juce::Slider::TextBoxRight,false,54,22);
        gain.setRange(-60.0,12.0,0.1);gain.setTextValueSuffix(" dB");gain.setDoubleClickReturnValue(true,0.0);
        gain.setValue(juce::Decibels::gainToDecibels(t.gain,-60.0f),juce::dontSendNotification);
        gain.setTooltip("Nondestructive playback gain. Double-click for 0 dB. The waveform grows with it; red lines mark 0 dBFS. Scarlett recording gain is unchanged.");
        gain.onDragStart = [this] { if (owner.editable()) owner.checkpoint(); };
        gain.onValueChange = [this] { if (owner.editable()) { model().gain = juce::Decibels::decibelsToGain((float)gain.getValue()); owner.changed(); } };
        gain.onDragEnd=[this]{owner.message("Track gain "+juce::String(gain.getValue(),1)+" dB. Red waveform lines are 0 dBFS; crossing them may clip. Effects can also change the final level.");};
        int presetIndex=1;
        for(const auto& preset:effectPresets())
        {
            effects.addItem(preset.name,presetIndex);
            if(t.effectPresetId==preset.id) effects.setSelectedId(presetIndex,juce::dontSendNotification);
            ++presetIndex;
        }
        effects.setTooltip("Playback/export preset. Original recordings are unchanged. Press ? for the full effect chain.");
        fx.setButtonText("FX"); fx.setToggleState(!t.effectsBypassed,juce::dontSendNotification);
        fx.setTooltip("FX off bypasses the selected preset. Stop playback before changing effects.");
        info.setButtonText("?"); info.setTooltip("What does this preset do?");
        effects.onChange=[this]
        {
            if(!owner.editable() || effects.getSelectedId()<1) return;
            owner.checkpoint(); model().effectPresetId=effectPresets()[(size_t)effects.getSelectedId()-1].id;
            model().effectsBypassed=false; fx.setToggleState(true,juce::dontSendNotification);
            owner.changed(); owner.message(juce::String(effectPreset(model().effectPresetId).name)+": "+effectPreset(model().effectPresetId).purpose+" Press ? for details.");
        };
        fx.onClick=[this]{if(owner.editable()){owner.checkpoint();model().effectsBypassed=!fx.getToggleState();owner.changed();}};
        info.onClick=[this]{juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            juce::String(effectPreset(model().effectPresetId).name)+(model().effectsBypassed?" (bypassed)":""),describeEffectPreset(model().effectPresetId),"Got it");};
        for(auto* c:std::initializer_list<juce::Component*>{&effects,&fx,&info}) addAndMakeVisible(c);
        verb.setButtonText("Verb");verb.setToggleState(t.reverb.enabled,juce::dontSendNotification);
        verb.setTooltip("Independent nondestructive reverb, after the EQ/compressor. Original takes stay dry.");
        style.addItem("Spring",1);style.addItem("Hall",2);style.addItem("Flerb",3);
        style.setSelectedId(t.reverb.style=="hall"?2:t.reverb.style=="flerb"?3:1,juce::dontSendNotification);
        style.setTooltip("Holy Grail-inspired flavors, not an exact EHX emulation.");
        reverbInfo.setButtonText("?");reverbInfo.setTooltip("Explain this reverb");
        mix.setName("Reverb wet/dry mix");mix.setSliderStyle(juce::Slider::LinearHorizontal);
        mix.setTextBoxStyle(juce::Slider::TextBoxRight,false,49,22);mix.setRange(0,100,1);
        mix.setTextValueSuffix("%");mix.setValue(t.reverb.mix*100,juce::dontSendNotification);
        mix.setTooltip("Wet/dry mix: 0% unchanged, 100% reverb only. Stop first to change.");
        mixLabel.setText("Reverb",juce::dontSendNotification);
        verb.onClick=[this]{if(owner.editable()){owner.checkpoint();model().reverb.enabled=verb.getToggleState();owner.changed();}};
        style.onChange=[this]{if(owner.editable()){owner.checkpoint();model().reverb.style=style.getSelectedId()==2?"hall":style.getSelectedId()==3?"flerb":"spring";owner.changed();}};
        mix.onDragStart=[this]{if(owner.editable())owner.checkpoint();};
        mix.onValueChange=[this]{if(owner.editable()){if(!mix.isMouseButtonDown())owner.checkpoint();model().reverb.mix=(float)mix.getValue()/100;owner.changed();}};
        reverbInfo.onClick=[this]{juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"Track reverb",describeReverb(model().reverb),"Got it");};
        for(auto* c:std::initializer_list<juce::Component*>{&verb,&style,&reverbInfo,&mix,&mixLabel})addAndMakeVisible(c);
        for (auto* c : std::initializer_list<juce::Component*>{&title,&input,&gain,&gainLabel}) addAndMakeVisible(c);
    }
    Track& model() { return owner.engine.session().tracks[(size_t)track]; }
    void resized() override
    {
        title.setBounds(15, 7, 214, 26);
        arm.setBounds(17, 39, 45, 25); mute.setBounds(68, 39, 29, 25); solo.setBounds(102, 39, 29, 25);
        input.setBounds(17,71,210,25);gainLabel.setBounds(15,103,35,22);gain.setBounds(48,103,109,22);
        fx.setBounds(10,138,44,25); effects.setBounds(55,138,137,25); info.setBounds(198,138,29,25);
        verb.setBounds(10,170,67,25);style.setBounds(80,170,112,25);reverbInfo.setBounds(198,170,29,25);
        mixLabel.setBounds(15,202,55,22);mix.setBounds(70,202,157,22);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(owner.selectedTrack == track ? juce::Colour(0xff222f40) : panel);
        g.setColour(trackColour(track)); g.fillRect(0, 0, 3, getHeight() - 1);
        g.setColour(juce::Colour(0xff0c111a)); g.fillRoundedRectangle(164, 111, 63, 6, 3);
        const float peak = owner.engine.inputPeak(model().inputChannel<0?owner.defaultInput:model().inputChannel);
        const float fraction = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(peak, -60.0f) + 60.0f) / 60.0f);
        g.setColour(peak >= 0.98f ? red : accent); g.fillRoundedRectangle(164, 111, 63 * fraction, 6, 3);
        g.setColour(muted); g.setFont(10.0f); g.drawText("IN", 163, 119, 25, 13, juce::Justification::left);
        g.setColour(bg); g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }
    void mouseDown(const juce::MouseEvent&) override { owner.selectedTrack = track; owner.selectedClipId.clear(); owner.timelineRepaint(); }
    void update() { const bool can = owner.editable(); for (auto* c : std::initializer_list<juce::Component*>{&title,&arm,&mute,&solo,&input,&gain,&effects,&fx,&verb,&style}) c->setEnabled(can);mix.setEnabled(can&&model().reverb.enabled); repaint(); }
private:
    StudioComponent& owner;
    int track;
    juce::Label title,gainLabel;
    juce::TextButton arm, mute, solo;
    juce::ComboBox input;
    juce::ComboBox effects;
    juce::ToggleButton fx;
    juce::TextButton info;
    juce::Slider gain;
    juce::ToggleButton verb;
    juce::ComboBox style;
    juce::TextButton reverbInfo;
    juce::Slider mix;
    juce::Label mixLabel;
};

class Timeline final : public juce::Component
{
public:
    explicit Timeline(StudioComponent& s) : owner(s) { setMouseCursor(juce::MouseCursor::CrosshairCursor); }
    void refreshTransportCursor(){if(!owner.editable()){hoverPlayhead=dragPlayhead=false;setMouseCursor(juce::MouseCursor::NormalCursor);}}
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
            const int y = owner.audioTop() + index * trackHeight;
            const float waveformCentre=y+trackHeight*0.55f,fullScale=70.0f;
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
                g.setColour(colour);g.setFont(11.0f);g.drawText("TAKE  "+formatTime(clip.startSeconds),rect.toNearestInt().reduced(8).removeFromTop(17),juce::Justification::left,true);
                if (!clip.audio) continue;
                float visibleRawPeak=0.0f;
                const float displayGain=(float)clip.gain*track.gain;
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
                    visibleRawPeak=std::max(visibleRawPeak,std::max(max,-min));
                    const float shownMin=waveformSample(min,clip,track),shownMax=waveformSample(max,clip,track);
                    g.setColour(std::max(shownMax,-shownMin)>1.0f?red:colour);
                    g.drawVerticalLine(px,waveformCentre-waveformDisplay(shownMax)*fullScale,
                                          waveformCentre-waveformDisplay(shownMin)*fullScale+0.6f);
                }
                if(w>275&&visibleRawPeak>0)
                {const auto rawDb=juce::Decibels::gainToDecibels(visibleRawPeak,-60.0f);const auto adjustedDb=juce::Decibels::gainToDecibels(visibleRawPeak*displayGain,-60.0f);
                const auto peakText=inputRating(rawDb)+" INPUT  |  raw "+juce::String(rawDb,1)+" dBFS  →  "+juce::String(adjustedDb,1)+" dBFS with Gain (pre-FX)";
                g.setColour(adjustedDb>0?red:ink.withAlpha(0.72f));g.setFont(10.0f);g.drawText(peakText,(int)x+110,y+13,std::max(0,(int)w-120),16,juce::Justification::right,true);}
            }
            if (owner.engine.isRecording() && track.armed && !owner.engine.isCountingIn())
            {
                const float x = owner.xAt(owner.engine.recordingStart());
                const float w = (float)std::max(2.0, (owner.engine.position() - owner.engine.recordingStart()) * owner.pixelsPerSecond);
                g.setColour(red.withAlpha(0.23f)); g.fillRoundedRectangle(x, (float)y + 8, w, trackHeight - 16.0f, 5);
                g.setColour(red); g.drawText("RECORDING", (int)x + 10, y + 13, 120, 20, juce::Justification::left);
                g.drawHorizontalLine((int)waveformCentre,std::max((float)headerWidth,x),std::min((float)getWidth(),x+w));
                if((size_t)index<owner.livePeaks.size())
                {
                    const auto& bins=owner.livePeaks[(size_t)index];
                    auto bin=std::lower_bound(bins.begin(),bins.end(),owner.viewStart-0.01,[](const LivePeak& p,double t){return p.seconds<t;});
                    for(;bin!=bins.end()&&bin->seconds<owner.viewStart+owner.viewDuration();++bin)
                    {const auto& peak=*bin;
                    const float px=owner.xAt(peak.seconds);if(px<headerWidth||px>=getWidth())continue;
                    const float low=peak.low*track.gain,high=peak.high*track.gain;
                    g.setColour(std::max(high,-low)>=1.0f?red:ink);
                    g.drawLine(px,waveformCentre-waveformDisplay(high)*fullScale,px,waveformCentre-waveformDisplay(low)*fullScale+0.7f,std::max(1.0f,(float)(peak.length*owner.pixelsPerSecond)));
                    }
                }
            }
            g.setColour(red.withAlpha(0.78f));g.drawHorizontalLine((int)(waveformCentre-fullScale),(float)headerWidth,(float)getWidth());g.drawHorizontalLine((int)(waveformCentre+fullScale),(float)headerWidth,(float)getWidth());
            g.setFont(juce::FontOptions(9.5f,juce::Font::bold));g.drawText("0 dBFS / CLIP",getWidth()-92,(int)(waveformCentre-fullScale)-14,84,13,juce::Justification::right);
            g.restoreState();
        }
        if(session.metronome)
        {
            const int top=rulerHeight;g.setColour(juce::Colour(0xff252d32));g.fillRect(0,top,getWidth(),72);
            g.setColour(juce::Colour(0xffffc778));g.setFont(juce::FontOptions(14.0f,juce::Font::bold));g.drawText("METRONOME",16,top+8,210,22,juce::Justification::left);
            g.setFont(12.0f);g.drawText(juce::String(session.bpm,0)+" BPM  |  4/4  |  not exported",16,top+33,220,22,juce::Justification::left);
            const double beatSeconds=60/session.bpm;
            g.saveState();g.reduceClipRegion(headerWidth,top,getWidth()-headerWidth,72);
            for(int64_t beat=(int64_t)std::floor(owner.viewStart/beatSeconds);beat*beatSeconds<owner.viewStart+owner.viewDuration();++beat)
            {
                const float x=owner.xAt(beat*beatSeconds);const bool down=beat%4==0;
                g.setColour(down?juce::Colour(0xffffc778):muted);
                const int width=std::max(2,(int)std::ceil(owner.pixelsPerSecond*0.016));
                for(int px=0;px<width;++px)
                {
                    float peak=0;for(int s=0;s<24;++s){const double local=0.016*(px+(s+0.5)/24)/width;peak=std::max(peak,std::abs(metronomeSample((beat*beatSeconds+local)*48000,beatSeconds*48000,48000)));}
                    g.drawVerticalLine((int)x+px,top+41-peak*155,top+41+peak*155);
                }
                if(down||beatSeconds*owner.pixelsPerSecond>45){g.setFont(10.0f);g.drawText(juce::String(beat/4+1)+"."+juce::String(beat%4+1),(int)x+5,top+3,44,14,juce::Justification::left);}
            }
            g.restoreState();
        }
        const float cursor = owner.xAt(owner.engine.position());
        if (cursor >= headerWidth && cursor < getWidth())
        {
            g.setColour(owner.engine.isRecording() ? red : (hoverPlayhead||dragPlayhead?accent:ink)); g.drawLine(cursor, 24, cursor, (float)getHeight(),hoverPlayhead||dragPlayhead?3.0f:1.4f);
            const float radius=hoverPlayhead||dragPlayhead?8.0f:5.0f;
            juce::Path p; p.addTriangle(cursor-radius, 16, cursor+radius, 16, cursor, 27); g.fillPath(p);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!owner.editable() || e.x < headerWidth) return;
        if(std::abs(e.position.x-owner.xAt(owner.engine.position()))<=8)
        {dragPlayhead=true;hoverPlayhead=true;setMouseCursor(juce::MouseCursor::DraggingHandCursor);repaint();return;}
        drag = false; mode = 0;
        owner.selectedClipId.clear();
        const int index = (e.y - owner.audioTop()) / trackHeight;
        const auto time = owner.timeAt(e.position.x);
        if (e.y >= owner.audioTop() && juce::isPositiveAndBelow(index, (int)owner.engine.session().tracks.size()))
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
        if(dragPlayhead && owner.editable()){owner.setPlayhead(owner.timeAt(e.position.x));repaint();return;}
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
    void mouseMove(const juce::MouseEvent& e) override
    {hoverPlayhead=owner.editable()&&e.x>=headerWidth&&std::abs(e.position.x-owner.xAt(owner.engine.position()))<=8;setMouseCursor(hoverPlayhead?juce::MouseCursor::PointingHandCursor:juce::MouseCursor::CrosshairCursor);repaint();}
    void mouseExit(const juce::MouseEvent&) override {if(!dragPlayhead){hoverPlayhead=false;setMouseCursor(juce::MouseCursor::CrosshairCursor);repaint();}}
    void mouseUp(const juce::MouseEvent& e) override
    {
        if(dragPlayhead){dragPlayhead=false;mouseMove(e);return;}
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
    bool hoverPlayhead=false,dragPlayhead=false;
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
    for (auto* c : std::initializer_list<juce::Component*>{&name,&clock,&tempoLabel,&latencyLabel,&status,&guide,&zoomLabel,&masterLabel,&tempo,&zoom,&masterGain,&clickButton,&countButton,&viewport,&scroll}) addAndMakeVisible(c);
    for (auto* b : {&newButton,&openButton,&saveButton,&exportButton,&settingsButton,&homeButton,&playButton,&stopButton,&recordButton,&returnButton,&addButton,&importButton,&splitButton,&deleteButton,&undoButton,&redoButton,&levelButton,&mixCheckButton})
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
    masterLabel.setText("Master",juce::dontSendNotification);masterLabel.setFont(juce::FontOptions(11.0f,juce::Font::bold));
    masterGain.setSliderStyle(juce::Slider::LinearHorizontal);masterGain.setTextBoxStyle(juce::Slider::TextBoxRight,false,54,22);masterGain.setRange(-24,12,0.1);masterGain.setTextValueSuffix(" dB");masterGain.setDoubleClickReturnValue(true,0);
    masterGain.setTooltip("Nondestructive gain after all tracks and effects. Double-click for 0 dB. Use Mix check before raising it.");
    masterGain.onDragStart=[this]{if(editable())checkpoint();};masterGain.onValueChange=[this]{if(editable()){engine.session().masterGain=juce::Decibels::decibelsToGain((float)masterGain.getValue());changed();}};
    clickButton.onClick = [this] { if(editable()) { checkpoint();engine.session().metronome = clickButton.getToggleState(); changed(true); } };
    countButton.onClick = [this] { if(editable()) { engine.session().countInBars = countButton.getToggleState() ? 1 : 0; changed(); } };
    newButton.onClick=[this]{newSession();}; openButton.onClick=[this]{openSession();}; saveButton.onClick=[this]{save(true);}; exportButton.onClick=[this]{exportFile();}; settingsButton.onClick=[this]{openAudioSetup();};
    homeButton.onClick=[this]{setPlayhead(0); viewStart=0; resized();}; returnButton.onClick=[this]{setPlayhead(lastRecordStart);};
    playButton.onClick=[this]{beginPlay();}; stopButton.onClick=[this]{stopTransport();}; recordButton.onClick=[this]{beginRecord();};
    addButton.onClick=[this]{addTrack();}; importButton.onClick=[this]{importFile();}; splitButton.onClick=[this]{splitSelected();}; deleteButton.onClick=[this]{deleteSelected();};
    undoButton.onClick=[this]{undo(false);}; redoButton.onClick=[this]{undo(true);}; levelButton.onClick=[this]{autoLevel();};mixCheckButton.onClick=[this]{checkMix();};
    timeline = std::make_unique<Timeline>(*this); viewport.setViewedComponent(timeline.get(), false); viewport.setScrollBarsShown(true, false);
    // The UI timer reads the horizontal scrollbar without involving the audio callback.
    juce::PropertiesFile::Options options; options.applicationName="Workspace"; options.filenameSuffix=".settings"; options.folderName="SimpleWindowsRecorder"; options.osxLibrarySubFolder="Application Support";
    workspace=std::make_unique<juce::PropertiesFile>(options);
    defaultInput=juce::jlimit(0,63,workspace->getIntValue("defaultInput",0));engine.setDefaultInput(defaultInput);
    for(auto* c:std::initializer_list<juce::Component*>{&defaultInputSelector,&diagnosticsButton,&retryButton,&defaultTracksButton})addAndMakeVisible(c);
    defaultTracksButton.setTooltip("Make every existing track follow the saved default input. Undo restores previous routing.");
    defaultTracksButton.onClick=[this]{if(!editable())return;checkpoint();for(auto& track:engine.session().tracks)track.inputChannel=-1;changed(true);message("All tracks now follow the saved default recording input.");};
    defaultInputSelector.setTooltip("Saved default recording input. New tracks all follow this. Track menus can explicitly override it.");
    defaultInputSelector.onChange=[this]{if(!editable())return;defaultInput=defaultInputSelector.getSelectedId()-1;engine.setDefaultInput(defaultInput);if(!previewMode){workspace->setValue("defaultInput",defaultInput);workspace->saveIfNeeded();}message("Default recording input: "+inputDescription(defaultInput));changed(true);};
    diagnosticsButton.onClick=[this]{showDiagnostics("Take One 0.7.0\nAudio status: "+audioSetup.message+"\nLatest error: "+observedError+"\nDefault input: "+inputDescription(defaultInput)+"\nProject: "+projectFile.getFullPathName());};
    retryButton.onClick=[this]{retryAudio();};
    tunerTitle.setText("NOTE MONITOR",juce::dontSendNotification);tunerTitle.setFont(juce::FontOptions(11.0f,juce::Font::bold));
    tunerDisplay.setFont(juce::FontOptions(15.0f,juce::Font::bold));
    tunerInput.setTooltip("Listen for one guitar note or sung note on this physical input. A4 = 440 Hz. No sound is routed to speakers.");
    for(auto* c:std::initializer_list<juce::Component*>{&tunerTitle,&tunerDisplay,&tunerInput})addAndMakeVisible(c);
    setSize(1180,740);
    if (previewMode) loadPreview();
    else { restoreWorkspace(); audioSetup=restoreAudioSetup(devices); devices.addAudioCallback(&engine); }
    refreshInputs();
    tunerInput.setSelectedId(juce::jlimit(1,tunerInput.getNumItems(),workspace->getIntValue("tunerInput",0)+1),juce::dontSendNotification);
    engine.setTunerInput(tunerInput.getSelectedId()-1);
    tunerInput.onChange=[this]{engine.setTunerInput(tunerInput.getSelectedId()-1);pitch={};pitchUpdated=0;pitchOpacity=0;if(!previewMode){workspace->setValue("tunerInput",tunerInput.getSelectedId()-1);workspace->saveIfNeeded();}};
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
bool StudioComponent::editable() const { return !engine.isBusy() && !exporting && !checkingMix && !audioWindow && !chooserPending; }
void StudioComponent::message(const juce::String& s) {if(s!=status.getText())logEvent(s);status.setText(s, juce::dontSendNotification); }
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
    for(int i=0;i<3;++i) { Track t; t.id=juce::Uuid().toString(); t.name=juce::StringArray{"Voice","Guitar","Bass"}[i]; t.armed=i==0; t.inputChannel=-1; engine.session().tracks.push_back(t); }
    const auto folder=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Simple Recorder Sessions")
        .getNonexistentChildFile("Song " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M"), "");
    projectFile=folder.getChildFile("Song.srproject"); engine.seek(0); viewStart=0;
    if(timeline) { rebuildTracks(); save(); message("New session. Every track follows your default recording input."); }
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
void StudioComponent::checkMix()
{
    if(!editable())return;if(engine.session().tracks.empty()){message("Record or import something before checking the mix.");return;}
    checkingMix=true;updateControls();message("Checking the processed mix loudness and headroom...");
    mixAnalysisTask=std::async(std::launch::async,[session=engine.session()]{return analyseMixLevel(session);});
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
juce::String StudioComponent::inputDescription(int channel) const
{
    if(auto* device=devices.getCurrentAudioDevice())
    {const auto channels=device->getInputChannelNames();return juce::String(channel+1)+": "+(juce::isPositiveAndBelow(channel,channels.size())?channels[channel]:"Unavailable")+" / "+device->getName();}
    return "Audio offline / channel "+juce::String(channel+1);
}
void StudioComponent::refreshInputs()
{
    const int tuner=tunerInput.getSelectedId();tunerInput.clear(juce::dontSendNotification);defaultInputSelector.clear(juce::dontSendNotification);
    for(int ch=0;ch<std::max({2,engine.inputChannelCount(),defaultInput+1});++ch){tunerInput.addItem(inputDescription(ch),ch+1);defaultInputSelector.addItem("Default input: "+inputDescription(ch),ch+1);}
    defaultInputSelector.setSelectedId(defaultInput+1,juce::dontSendNotification);tunerInput.setSelectedId(std::max(1,tuner),juce::dontSendNotification);
}
void StudioComponent::retryAudio()
{
    if(!editable())return;message("Retrying saved audio setup...");devices.removeAudioCallback(&engine);devices.closeAudioDevice();engine.clearError();observedError.clear();audioSetup=restoreAudioSetup(devices);devices.addAudioCallback(&engine);refreshInputs();rebuildTracks();message(audioSetup.message);
}
void StudioComponent::handleUnexpectedError(const juce::String& error)
{
    applicationFault=true;engine.stop();observedError=error;message("ERROR: "+error);updateControls();
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Take One error",error+"\nTransport stopped. Open Diagnostics to copy the log. Save if possible and restart before recording again.");
}
void StudioComponent::openAudioSetup()
{
    if(!editable()) return; save(); devices.removeAudioCallback(&engine); devices.closeAudioDevice();
    auto w=std::make_unique<SetupWindow>();
    w->onClose=[safe=juce::Component::SafePointer<StudioComponent>(this)]
    { juce::MessageManager::callAsync([safe]{if(!safe)return; safe->audioWindow.reset();safe->engine.clearError();safe->observedError.clear(); safe->audioSetup=restoreAudioSetup(safe->devices); safe->devices.addAudioCallback(&safe->engine);safe->refreshInputs(); safe->rebuildTracks(); safe->message(safe->audioSetup.message);}); };
    audioWindow=std::move(w);
}
void StudioComponent::beginPlay()
{
    if(applicationFault)return;
    if(!editable()) return; if(!engine.play(engine.position())) message(engine.lastError()); else message("Playing. Space to stop."); updateControls();
}
void StudioComponent::beginRecord()
{
    if(applicationFault)return;
    if(!editable()) return; checkpoint(); lastRecordStart=engine.position();
    audioSetup=calibrationForActiveSetup(devices);
    double correction=audioSetup.compensationMs;
    if(!audioSetup.calibrated)
        if(auto* d=devices.getCurrentAudioDevice()) correction=1000.0*(d->getInputLatencyInSamples()+d->getOutputLatencyInSamples())/d->getCurrentSampleRate();
    if(!engine.record(lastRecordStart,correction,projectFile.getSiblingFile("Media"))) {history.pop_back();message(engine.lastError());}
    else {recordViewStart=viewStart;returnAfterRecording=true;livePeaks.clear();livePeaks.resize(engine.session().tracks.size());message("Recording from " + formatTime(lastRecordStart) + ". Stop returns here automatically.");}
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
int StudioComponent::audioTop() const {return rulerHeight+(engine.session().metronome?72:0);}
double StudioComponent::timeAt(float x) const { return std::max(0.0,viewStart+(x-headerWidth)/pixelsPerSecond); }
float StudioComponent::xAt(double t) const { return (float)(headerWidth+(t-viewStart)*pixelsPerSecond); }
void StudioComponent::rebuildTracks()
{
    trackHeaders.clear();
    for(int i=0;i<(int)engine.session().tracks.size();++i) {auto h=std::make_unique<TrackHeader>(*this,i); timeline->addAndMakeVisible(*h); trackHeaders.push_back(std::move(h));}
    name.setText(engine.session().name,juce::dontSendNotification);tempo.setValue(engine.session().bpm,juce::dontSendNotification);clickButton.setToggleState(engine.session().metronome,juce::dontSendNotification);countButton.setToggleState(engine.session().countInBars>0,juce::dontSendNotification);masterGain.setValue(juce::Decibels::gainToDecibels(engine.session().masterGain,-24.0f),juce::dontSendNotification);resized();updateControls();
}
void StudioComponent::updateControls()
{
    const bool can=editable();
    defaultInputSelector.setEnabled(can);retryButton.setEnabled(can&&!applicationFault);defaultTracksButton.setEnabled(can);
    for(auto* c:std::initializer_list<juce::Component*>{&newButton,&openButton,&saveButton,&exportButton,&settingsButton,&homeButton,&returnButton,&playButton,&recordButton,&addButton,&importButton,&splitButton,&deleteButton,&levelButton,&mixCheckButton,&masterGain,&tempo,&clickButton,&countButton,&name,&clock}) c->setEnabled(can);
    stopButton.setEnabled(engine.isBusy()); undoButton.setEnabled(can&&!history.empty()); redoButton.setEnabled(can&&!future.empty());
    playButton.setEnabled(can&&engine.sampleRate()>0&&!applicationFault);recordButton.setEnabled(can&&engine.sampleRate()>0&&!applicationFault);
    const bool offline=!previewMode&&engine.sampleRate()<=0;
    guide.setColour(juce::Label::textColourId,offline||applicationFault?red:muted);
    const auto health=applicationFault?"ERROR: "+observedError:offline?"AUDIO OFFLINE — "+(observedError.isEmpty()?audioSetup.message:observedError):"Recording default: "+inputDescription(defaultInput);
    guide.setText(health,juce::dontSendNotification);guide.setTooltip(health);
    for(auto& h:trackHeaders) h->update();
    latencyLabel.setText(audioSetup.calibrated?juce::String(audioSetup.compensationMs,1)+" ms correction":"Driver timing - calibrate in Audio setup",juce::dontSendNotification);
}
void StudioComponent::timerCallback()
{
    LivePeak peak;while(engine.readLivePeak(peak))if(juce::isPositiveAndBelow(peak.track,(int)livePeaks.size()))
    {auto& bins=livePeaks[(size_t)peak.track];if(bins.size()<180001)bins.push_back(peak);}
    if(engine.poll())
    {dirty=true;pendingRebuild=true;juce::String advice;
    for(const auto& track:engine.session().tracks)if(track.armed)for(const auto& clip:track.clips)if(std::abs(clip.startSeconds-lastRecordStart)<0.002)
    {const auto level=analyseRecordingLevel(clip);if(level.valid)advice<<track.name+": "+level.rating+" — raw peak "+juce::String(level.peakDb,1)+" dBFS, active average "+juce::String(level.activeRmsDb,1)+" dBFS. "+level.advice;break;}
    message(advice.isNotEmpty()?advice:"Take saved. Punch-in replaced only the recorded span. Undo restores the previous take.");}
    if(returnAfterRecording&&!engine.isBusy()) {returnAfterRecording=false;viewStart=recordViewStart;dirty=true;resized();}
    const double now=juce::Time::getMillisecondCounterHiRes();
    PitchResult measured;const bool hasMeasurement=engine.pollPitch(measured);
    updateHeldPitch(pitch,pitchUpdated,pitchOpacity,hasMeasurement?&measured:nullptr,now,engine.sampleRate()>0);
    if(pitch.midi>=0)
    {
        const juce::String tuning=std::abs(pitch.cents)<=5?"In tune":pitch.cents<0?"Flat":"Sharp";
        const int cents=(int)std::llround(pitch.cents);
        tunerDisplay.setText(pitchName(pitch.midi)+"   "+juce::String(pitch.hz,1)+" Hz   "+tuning+"  "+(cents>0?"+":"")+juce::String(cents)+" cents",juce::dontSendNotification);
        tunerDisplay.setColour(juce::Label::textColourId,(std::abs(pitch.cents)<=5?accent:ink).withMultipliedAlpha(pitchOpacity));
    }
    else {tunerDisplay.setText("Play or sing one note  |  A4 = 440 Hz",juce::dontSendNotification);tunerDisplay.setColour(juce::Label::textColourId,muted);}
    repaint(0,192,getWidth(),42);
    const auto error=engine.lastError(); if(error.isNotEmpty() && error!=observedError) {observedError=error;message("ERROR: "+error);if(engine.sampleRate()<=0){devices.removeAudioCallback(&engine);devices.closeAudioDevice();}}
    if(exporting && exportTask.valid() && exportTask.wait_for(std::chrono::seconds(0))==std::future_status::ready) {auto r=exportTask.get();exporting=false;message(r.wasOk()?"Stereo WAV exported.":r.getErrorMessage());}
    if(checkingMix&&mixAnalysisTask.valid()&&mixAnalysisTask.wait_for(std::chrono::seconds(0))==std::future_status::ready)
    {const auto analysis=mixAnalysisTask.get();checkingMix=false;if(!analysis.valid)message("Mix check could not find enough audible material.");else
    {const double current=juce::Decibels::gainToDecibels(engine.session().masterGain,-24.0f),suggested=juce::jlimit(-24.0,12.0,current+analysis.suggestedMasterDb);
    const auto text="Integrated loudness: "+juce::String(analysis.loudnessLufs,1)+" LUFS\nSample peak: "+juce::String(analysis.samplePeakDb,1)+" dBFS\n\n"+analysis.advice+"\n\nSuggested Master: "+juce::String(suggested,1)+" dB (a "+(analysis.suggestedMasterDb>=0?"+":"")+juce::String(analysis.suggestedMasterDb,1)+" dB change). This is guidance, not automatic mastering.";
    message(analysis.rating+" — "+juce::String(analysis.loudnessLufs,1)+" LUFS, "+juce::String(analysis.samplePeakDb,1)+" dBFS peak.");juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,analysis.rating,text,"Got it");}}
    if(pendingRebuild&&!engine.isBusy()) {pendingRebuild=false;rebuildTracks();}
    if(dirty && editable()) save();
    const double p=engine.position();
    if(!clock.isBeingEdited() && p!=displayedPosition) {clock.setText(formatTime(p),juce::dontSendNotification);displayedPosition=p;}
    if(engine.isCountingIn()) message("Count in: " + juce::String(engine.countInBeatsRemaining()) + " beats. Recording will begin at " + formatTime(lastRecordStart));
    else if(engine.isRecording()) message("Recording. Stop saves the take and returns to its start.");
    if(engine.isBusy() && p>viewStart+viewDuration()*0.95) {viewStart=std::max(0.0,p-viewDuration()*0.2);resized();}
    else if(std::abs(scroll.getCurrentRangeStart()-viewStart)>0.001) {viewStart=scroll.getCurrentRangeStart();}
    updateControls(); timeline->refreshTransportCursor();timeline->repaint();
    if(closing&&!engine.isBusy()&&!exporting&&!checkingMix){if(save())juce::JUCEApplication::getInstance()->quit();else closing=false;}
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
void StudioComponent::requestClose(){closing=true;if(engine.isBusy())engine.stop();else if(!exporting&&!checkingMix){if(save())juce::JUCEApplication::getInstance()->quit();else closing=false;}}
void StudioComponent::paint(juce::Graphics& g)
{
    g.fillAll(bg);g.setColour(panel);g.fillRect(0,68,getWidth(),76);g.setColour(accent);g.fillRoundedRectangle(20,22,5,28,2);
    g.setColour(muted);g.setFont(11.0f);
    const float middle=(float)getWidth()-115;
    g.setColour(muted);g.drawLine(middle-70,217,middle+70,217,1);g.drawLine(middle,210,middle,224,1);
    if(pitch.midi>=0){g.setColour((std::abs(pitch.cents)<=5?accent:juce::Colour(0xffffc778)).withMultipliedAlpha(pitchOpacity));const float x=middle+(float)juce::jlimit(-50.0,50.0,pitch.cents)*1.4f;g.fillEllipse(x-4,213,8,8);}
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
    latencyLabel.setBounds(694,76,std::max(80,w-710),24);latencyLabel.setFont(12.0f);latencyLabel.setColour(juce::Label::textColourId,muted);
    defaultInputSelector.setBounds(694,107,std::max(100,w-710),25);diagnosticsButton.setBounds(18,h-30,112,24);retryButton.setBounds(138,h-30,100,24);defaultTracksButton.setBounds(246,h-30,140,24);
    masterLabel.setBounds(w-455,h-30,48,24);masterGain.setBounds(w-407,h-30,160,24);mixCheckButton.setBounds(w-237,h-30,100,24);
    x=18;for(auto* b:{&addButton,&importButton,&undoButton,&redoButton,&splitButton,&deleteButton,&levelButton}){const int width=b==&importButton?104:(b==&levelButton?84:66);b->setBounds(x,157,width,27);x+=width+6;}
    zoomLabel.setBounds(w-190,157,40,27);zoom.setBounds(w-150,157,130,27);
    tunerTitle.setBounds(18,198,105,30);tunerInput.setBounds(126,200,95,26);tunerDisplay.setBounds(230,197,std::max(300,w-455),32);
    viewport.setBounds(16,238,w-32,std::max(100,h-331));
    timeline->setSize(viewport.getWidth()-16,std::max(viewport.getHeight(),audioTop()+(int)trackHeaders.size()*trackHeight+20));
    for(int i=0;i<(int)trackHeaders.size();++i)trackHeaders[(size_t)i]->setBounds(0,audioTop()+i*trackHeight,headerWidth,trackHeight);
    scroll.setBounds(16+headerWidth,h-86,w-32-headerWidth,12); scroll.setRangeLimits(0,std::max({120.0,engine.duration()+30,viewStart+viewDuration()}));scroll.setCurrentRange(viewStart,viewDuration(),juce::dontSendNotification);
    status.setBounds(20,h-65,w-40,30);status.setFont(12.0f);status.setColour(juce::Label::textColourId,muted);
}
bool StudioComponent::runFeedbackUiChecks(juce::String& report)
{
    {Track track;track.gain=2.0f;Clip clip;clip.gain=0.5;if(std::abs(waveformSample(0.75f,clip,track)-0.75f)>1e-6f){report+="FAIL feedback UI: waveform gain disagrees with playback gain.\n";return false;}}
    if(std::abs(waveformDisplay(0.25f)-0.5f)>1e-6f||waveformDisplay(1.0f)!=1.0f||waveformDisplay(1.2f)<=1.0f){report+="FAIL feedback UI: perceptual waveform scale lost the 0 dBFS reference.\n";return false;}
    {PitchResult held{440,0,69,1},silence{};double last=1000;float opacity=1;
    updateHeldPitch(held,last,opacity,&silence,1300,true);if(held.midi!=69||opacity<0.99f)return false;
    updateHeldPitch(held,last,opacity,nullptr,2500,true);if(held.midi!=69||opacity>=1||opacity<=0)return false;
    updateHeldPitch(held,last,opacity,nullptr,2900,true);if(held.midi>=0||opacity!=0)return false;}
    StudioComponent studio(true);studio.stopTimer();
    const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("recorder-ui-test-"+juce::Uuid().toString());
    struct Cleanup {juce::File f;~Cleanup(){if(f.getFileName().startsWith("recorder-ui-test-"))f.deleteRecursively();}} cleanup{folder};
    const auto fail=[&](const juce::String& s){report+="FAIL feedback UI: "+s+"\n";return false;};
    const auto event=[&](juce::Point<float> point,juce::Point<float> down,bool dragged)
    {return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,studio.timeline.get(),studio.timeline.get(),juce::Time::getCurrentTime(),down,juce::Time::getCurrentTime(),1,dragged);};
    const juce::Point<float> start{studio.xAt(30),(float)(studio.audioTop()+trackHeight+60)};
    studio.timeline->mouseMove(event(start,start,false));
    if(studio.timeline->getMouseCursor()!=juce::MouseCursor(juce::MouseCursor::PointingHandCursor))return fail("missing hand cursor");
    studio.timeline->mouseDown(event(start,start,false));
    const auto end=start.translated(64,0);studio.timeline->mouseDrag(event(end,start,true));studio.timeline->mouseUp(event(end,start,true));
    if(std::abs(studio.engine.position()-32)>1e-6||studio.engine.session().tracks[1].clips[0].startSeconds!=0||!studio.history.empty())return fail("playhead drag edited a clip or failed to seek");
    const int withClick=studio.trackHeaders[0]->getY();studio.engine.session().metronome=false;studio.resized();
    if(withClick-studio.trackHeaders[0]->getY()!=72)return fail("metronome lane layout offset");
    studio.engine.session().metronome=true;studio.resized();studio.setPlayhead(30);
    studio.engine.prepareForDevice(48000,256,2);studio.engine.session().countInBars=0;studio.projectFile=folder.getChildFile("song.srproject");
    studio.beginRecord();if(!studio.engine.isRecording())return fail("could not start simulated UI take");
    std::array<float,256> input{},left{},right{};const float* inputs[]{input.data(),nullptr};float* outputs[]{left.data(),right.data()};
    for(int block=0;block<375;++block)
    {
        for(int i=0;i<256;++i){const double time=(block*256+i)/48000.0;input[(size_t)i]=(float)(0.6*std::sin(time*juce::MathConstants<double>::twoPi*110)*(0.5+0.5*std::sin(time*12)));}
        studio.engine.audioDeviceIOCallbackWithContext(inputs,2,outputs,2,256,{});
        if(block%8==0){studio.timerCallback();juce::Thread::sleep(1);}
    }
    studio.timerCallback();if(studio.livePeaks.empty()||studio.livePeaks[0].empty())return fail("UI didn't receive live waveform");
    {auto image=studio.createComponentSnapshot(studio.getLocalBounds());juce::FileOutputStream stream(juce::File::getCurrentWorkingDirectory().getChildFile("feedback-live-preview.png"));stream.setPosition(0);stream.truncate();juce::PNGImageFormat png;png.writeImageToStream(image,stream);}
    studio.stopTransport();
    for(int n=0;n<3000&&studio.engine.isBusy();++n){studio.engine.audioDeviceIOCallbackWithContext(inputs,2,outputs,2,256,{});studio.timerCallback();juce::Thread::sleep(1);}
    if(studio.engine.isBusy()||std::abs(studio.engine.position()-30)>1e-6||std::abs(studio.viewStart-10)>1e-6)return fail("UI stop did not return cursor/view");
    studio.beginPlay();if(std::abs(studio.engine.position()-30)>1e-6)return fail("audition didn't start at punch");
    studio.engine.stop();studio.engine.audioDeviceIOCallbackWithContext(inputs,2,outputs,2,256,{});studio.engine.audioDeviceStopped();
    report+="PASS feedback UI: tuner holds a valid note for 1.8 seconds and fades; playhead hand/drag over a clip leaves media unchanged; metronome offset; live waveform delivered; Stop restores anchor/view and Play auditions there.\n";
    return true;
}
void StudioComponent::loadPreview()
{
    engine.session()=Session{};engine.session().name="Evening ideas";engine.session().metronome=true;
    for(int i=0;i<4;++i)
    {
        Track t;t.id=juce::Uuid().toString();t.name=juce::StringArray{"Voice","Acoustic guitar","Bass","Harmony"}[i];t.armed=i==0;t.inputChannel=i==0?0:1;
        t.effectPresetId=juce::StringArray{"lead-vocal","acoustic-guitar","bass","warm-vocal"}[i];
        t.reverb.enabled=i<2;t.reverb.style=i==0?"hall":"spring";
        if(i<3){Clip c;c.id=juce::Uuid().toString();c.startSeconds=i==0?7:0;c.lengthSeconds=i==0?15:37;c.sampleRate=48000;c.audio=std::make_shared<juce::AudioBuffer<float>>(1,(int)(c.lengthSeconds*48000));for(int n=0;n<c.audio->getNumSamples();++n){const double time=n/48000.0;const double env=std::pow(std::max(0.0,std::sin(time*(i==0?3:6))),i==0?0.5:3.0);c.audio->setSample(0,n,(float)(0.5*env*std::sin(time*juce::MathConstants<double>::twoPi*(110+55*i))));}t.clips.push_back(c);}
        engine.session().tracks.push_back(t);
    }
    engine.seek(30);viewStart=10;audioSetup.calibrated=true;audioSetup.compensationMs=33.8;
}
}
