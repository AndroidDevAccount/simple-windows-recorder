// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
namespace studio
{
template<class T,int N> class AnalysisQueue
{
public:
    bool push(const T& value) noexcept {int a,n,b,m;fifo.prepareToWrite(1,a,n,b,m);if(n+m==0)return false;data[(size_t)(n?a:b)]=value;fifo.finishedWrite(1);return true;}
    bool pop(T& value) noexcept {int a,n,b,m;fifo.prepareToRead(1,a,n,b,m);if(n+m==0)return false;value=data[(size_t)(n?a:b)];fifo.finishedRead(1);return true;}
private:
    juce::AbstractFifo fifo{N};std::array<T,N> data{};
};
struct LivePeak { int track=0; double seconds=0, length=0; float low=0,high=0; };
struct PitchResult {double hz=0,cents=0;int midi=-1;float confidence=0;};
PitchResult detectPitch(const float*,int,double);
juce::String pitchName(int midi);
float metronomeSample(double frame,double framesPerBeat,double rate) noexcept;
class InputAnalysis
{
public:
    void prepare(double rate) noexcept;
    void setInput(int channel) noexcept {selected.store(channel);}
    int input() const noexcept {return selected.load();}
    void capture(const float* samples,int count,int physicalChannel) noexcept;
    // Consumer (UI thread) only; analysis deliberately never runs on audio thread.
    bool poll(PitchResult& result);
private:
    struct Frame {std::array<float,2048> samples{};double rate=12000;int channel=0;unsigned epoch=0,sequence=0;};
    AnalysisQueue<Frame,5> queue;
    Frame frame;
    std::atomic<int> selected{0};std::atomic<unsigned> epoch{0};
    std::atomic<unsigned> sequence{0};
    int active=-1,decimation=4,partial=0,used=0;float sum=0;
    double sampleRate=48000;
};
bool runFeedbackTests(juce::String&);
}
