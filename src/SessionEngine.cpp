// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SessionEngine.h"
#include "TrackEffects.h"
#include "Diagnostics.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>

namespace studio
{
namespace
{
constexpr int maxInputs = 64;
constexpr int maxArmedTracks = 32;
constexpr std::int64_t captureMemoryBudget = 256LL * 1024 * 1024;
enum class Transport { idle, playing, countIn, recording, draining, finalizing };

}

struct SessionEngine::Impl
{
    struct Take
    {
        std::size_t trackIndex = 0;
        int physicalInput = 0, callbackInput = 0;
        juce::File file;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        float low=0,high=0;int peakSamples=0;int64_t peakStart=0;
    };

    Session model, playback, beforeRecord, undoSession;
    bool undoAvailable = false;
    juce::String error, writerError;
    std::atomic<Transport> transport { Transport::idle };
    std::atomic<bool> stopRequested { false }, captureFinished { false }, workerDone { false };
    std::atomic<bool> overflow { false }, deviceLost { false }, writeFailed { false }, limitReached { false };
    std::atomic<double> rate { 0.0 }, displayedPosition { 0.0 }, recordAnchor { 0.0 }, recordEnd { 0.0 };
    std::atomic<std::int64_t> recordFrames { 0 }, countInRemaining { 0 };
    std::atomic<int> physicalInputs { 0 };
    std::atomic<float> peakOut { 0.0f };
    std::array<std::atomic<float>, maxInputs> peaks {};
    std::array<int, maxInputs> physicalToCallback {};

    // These values are prepared before publishing a running transport state,
    // then owned exclusively by the audio callback until finalization.
    double start = 0.0, runningRate = 48000.0, framesPerBeat = 28800.0;
    std::int64_t timelineFrames = 0, capturedFrames = 0, delayFrames = 0;
    std::int64_t captureTarget = 0, totalCountIn = 0, maxCaptureFrames = 0;
    bool hasSolo = false;
    std::vector<Take> takes;
    std::vector<TrackEffects> effects;
    std::array<float,1024> scratchLeft {}, scratchRight {};
    juce::AudioBuffer<float> ring;
    std::unique_ptr<juce::AbstractFifo> fifo;
    std::thread writerThread;
    AnalysisQueue<LivePeak,8193> live;
    InputAnalysis analysis;
    int defaultInput=0;
    std::atomic<int> driverErrorState{0};std::array<char,1024> driverError{};

    Impl() { physicalToCallback.fill(-1); }

    void preparePlayback(double seconds)
    {
        playback = model;
        hasSolo = std::any_of(playback.tracks.begin(), playback.tracks.end(),
                            [] (const Track& t) { return t.solo; });
        start = std::max(0.0, seconds);
        runningRate = rate.load();
        effects.resize(playback.tracks.size());
        for(size_t i=0;i<effects.size();++i)
            effects[i].prepare(playback.tracks[i].effectPresetId,playback.tracks[i].effectsBypassed,runningRate,playback.tracks[i].reverb);
        framesPerBeat = runningRate * 60.0 / juce::jlimit(30.0, 300.0, model.bpm);
        timelineFrames = 0;
        stopRequested.store(false);
        displayedPosition.store(start);
        error.clear();
    }

    void finishCapture() noexcept
    {
        // Publish all producer writes before the worker drains the final block.
        captureFinished.store(true, std::memory_order_release);
        transport.store(Transport::finalizing, std::memory_order_release);
    }

    void stopAtBoundary() noexcept
    {
        const auto mode = transport.load(std::memory_order_acquire);
        if (mode == Transport::playing)
            transport.store(Transport::idle, std::memory_order_release);
        else if (mode == Transport::countIn)
        {
            countInRemaining.store(0);
            recordFrames.store(0);
            recordEnd.store(start);
            finishCapture();
        }
        else if (mode == Transport::recording)
        {
            recordFrames.store(timelineFrames);
            recordEnd.store(start + static_cast<double>(timelineFrames) / runningRate);
            captureTarget = timelineFrames + delayFrames;
            if (capturedFrames >= captureTarget)
                finishCapture();
            else
                transport.store(Transport::draining, std::memory_order_release);
        }
    }

    void interrupted() noexcept
    {
        const auto mode = transport.load(std::memory_order_acquire);
        if (mode == Transport::recording || mode == Transport::draining || mode == Transport::countIn)
        {
            deviceLost.store(true);
            // No callbacks remain to supply the tail. Only expose audio for
            // which the latency-adjusted source actually exists on disk.
            const auto available = std::max<std::int64_t>(0, capturedFrames - delayFrames);
            const auto length = mode == Transport::draining ? std::min(recordFrames.load(), available) : available;
            recordFrames.store(length);
            recordEnd.store(start + static_cast<double>(length) / runningRate);
            displayedPosition.store(recordEnd.load());
            countInRemaining.store(0);
            finishCapture();
        }
        else if (mode == Transport::playing)
            transport.store(Transport::idle, std::memory_order_release);
    }

    bool capture(const float* const* inputs, int inputCount, int offset, int count) noexcept
    {
        if (fifo->getFreeSpace() < count)
        {
            overflow.store(true);
            const auto usable = std::max<std::int64_t>(0, capturedFrames - delayFrames);
            recordFrames.store(usable);
            recordEnd.store(start + static_cast<double>(usable) / runningRate);
            displayedPosition.store(recordEnd.load());
            finishCapture();
            return false;
        }

        int first = 0, firstCount = 0, second = 0, secondCount = 0;
        fifo->prepareToWrite(count, first, firstCount, second, secondCount);
        for (std::size_t t = 0; t < takes.size(); ++t)
        {
            auto& take=takes[t];
            const auto input = takes[t].callbackInput;
            const auto* source = input >= 0 && input < inputCount ? inputs[input] : nullptr;
            for(int i=0;i<count;++i)
            {
                const auto frame=capturedFrames+i-delayFrames;if(frame<0)continue;
                if(take.peakSamples==0)take.peakStart=frame;
                const float value=source?source[offset+i]:0;take.low=std::min(take.low,value);take.high=std::max(take.high,value);
                if(++take.peakSamples>=std::max(1,(int)(runningRate*0.01)))
                {live.push({(int)take.trackIndex,recordAnchor.load()+take.peakStart/runningRate,take.peakSamples/runningRate,take.low,take.high});take.low=take.high=0;take.peakSamples=0;}
            }
            auto* destination = ring.getWritePointer(static_cast<int>(t));
            if (source != nullptr)
            {
                juce::FloatVectorOperations::copy(destination + first, source + offset, firstCount);
                if (secondCount > 0)
                    juce::FloatVectorOperations::copy(destination + second, source + offset + firstCount, secondCount);
            }
            else
            {
                juce::FloatVectorOperations::clear(destination + first, firstCount);
                if (secondCount > 0)
                    juce::FloatVectorOperations::clear(destination + second, secondCount);
            }
        }
        fifo->finishedWrite(firstCount + secondCount);
        capturedFrames += firstCount + secondCount;
        return true;
    }

    void render(float* const* outputs, int outputCount, int offset, int count, bool punching) noexcept
    {
        for (size_t t=0;t<playback.tracks.size();++t)
        {
            const auto& track=playback.tracks[t];
            if (track.mute || (hasSolo && !track.solo) || (punching && track.armed))
                continue;
            for(int processed=0;processed<count;processed+=1024)
            {
                const int n=std::min(1024,count-processed);
                renderTrackAudio(track,start+(double)(timelineFrames+processed)/runningRate,runningRate,
                                 scratchLeft.data(),scratchRight.data(),n);
                effects[t].process(scratchLeft.data(),scratchRight.data(),n);
                for(int channel=0;channel<outputCount;++channel)
                    if(outputs[channel]) juce::FloatVectorOperations::addWithMultiply(outputs[channel]+offset+processed,
                        channel==0?scratchLeft.data():scratchRight.data(),track.gain,n);
            }
        }
        if(playback.masterGain!=1.0f)
            for(int channel=0;channel<outputCount;++channel)
                if(outputs[channel])juce::FloatVectorOperations::multiply(outputs[channel]+offset,playback.masterGain,count);
        if (playback.metronome)
            for (int i = 0; i < count; ++i)
            {
                const auto click = metronomeSample(start * runningRate + static_cast<double>(timelineFrames + i), framesPerBeat, runningRate);
                for (int channel = 0; channel < outputCount; ++channel)
                    if (outputs[channel] != nullptr)
                        outputs[channel][offset + i] += click;
            }
    }

    void runWriter()
    {
        // This worker owns disk I/O and source-buffer allocation. The audio
        // callback only copies to the preallocated SPSC ring above.
        try
        {
            for (;;)
            {
                const auto ready = fifo->getNumReady();
                if (ready == 0)
                {
                    if (captureFinished.load(std::memory_order_acquire))
                    {
                        // Recheck after the acquire: final producer writes may
                        // have arrived between the first read and completion.
                        if (fifo->getNumReady() == 0)
                            break;
                        continue;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                int first = 0, firstCount = 0, second = 0, secondCount = 0;
                fifo->prepareToRead(std::min(ready, 8192), first, firstCount, second, secondCount);
                for (std::size_t t = 0; t < takes.size(); ++t)
                {
                    const float* source[] { ring.getReadPointer(static_cast<int>(t), first) };
                    bool ok = takes[t].writer->writeFromFloatArrays(source, 1, firstCount);
                    if (secondCount > 0)
                    {
                        source[0] = ring.getReadPointer(static_cast<int>(t), second);
                        ok = takes[t].writer->writeFromFloatArrays(source, 1, secondCount) && ok;
                    }
                    if (!ok)
                    {
                        writeFailed.store(true);
                        stopRequested.store(true);
                    }
                }
                fifo->finishedRead(firstCount + secondCount);
            }
            for (auto& take : takes)
            {
                if (!take.writer->flush())
                    writeFailed.store(true);
                take.writer.reset();
            }
            if (writeFailed.load())
                writerError = "The recording drive could not keep writing. Your earlier takes are safe; the unfinished WAV files are kept in the Takes folder.";
            else if (recordFrames.load() > 0)
            {
                juce::WavAudioFormat format;
                for (auto& take : takes)
                {
                    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(take.file.createInputStream().release(), true));
                    if (!reader || reader->lengthInSamples > std::numeric_limits<int>::max())
                    {
                        writerError = "The take was recorded, but could not be loaded for playback. Its WAV file is safe in the Takes folder.";
                        break;
                    }
                    take.audio = std::make_shared<juce::AudioBuffer<float>>(1, static_cast<int>(reader->lengthInSamples));
                    if (!reader->read(take.audio.get(), 0, take.audio->getNumSamples(), 0, true, false))
                    {
                        writerError = "The recorded WAV could not be read completely. Its original file has been kept.";
                        break;
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            writeFailed.store(true);
            stopRequested.store(true);
            writerError = "Recording could not finish: " + juce::String(e.what()) + ". Existing takes have been preserved.";
        }
        workerDone.store(true, std::memory_order_release);
    }
};

SessionEngine::SessionEngine() : impl(std::make_unique<Impl>()) {}
SessionEngine::~SessionEngine()
{
    // The owner must remove this callback from its AudioDeviceManager first.
    impl->interrupted();
    impl->captureFinished.store(true, std::memory_order_release);
    if (impl->writerThread.joinable())
        impl->writerThread.join();
}

Session& SessionEngine::session() noexcept { return impl->model; }
const Session& SessionEngine::session() const noexcept { return impl->model; }

bool SessionEngine::play(double startSeconds)
{
    if (isBusy())
        return false;
    if (impl->rate.load() <= 0.0)
    {
        impl->error = "Choose an audio device before pressing Play.";
        return false;
    }
    impl->preparePlayback(startSeconds);
    impl->transport.store(Transport::playing, std::memory_order_release);
    return true;
}

bool SessionEngine::record(double startSeconds, double compensationMs, const juce::File& takesDirectory)
{
    if (isBusy())
        return false;
    if (impl->rate.load() <= 0.0)
    {
        impl->error = "Choose an audio device before recording.";
        return false;
    }
    impl->error.clear();
    impl->takes.clear();
    for (std::size_t i = 0; i < impl->model.tracks.size(); ++i)
    {
        const auto& track = impl->model.tracks[i];
        if (!track.armed)
            continue;
        const int channel=track.inputChannel<0?impl->defaultInput:track.inputChannel;
        if (channel < 0 || channel >= maxInputs
            || impl->physicalToCallback[static_cast<std::size_t>(channel)] < 0)
        {
            impl->error = "The input selected for " + track.name + " is unavailable. Choose an active microphone or instrument input.";
            impl->takes.clear();
            return false;
        }
        Impl::Take take;
        take.trackIndex = i;
        take.physicalInput = channel;
        take.callbackInput = impl->physicalToCallback[static_cast<std::size_t>(channel)];
        impl->takes.push_back(std::move(take));
    }
    if (impl->takes.empty() || impl->takes.size() > maxArmedTracks)
    {
        impl->error = impl->takes.empty() ? "Arm a track with its red Record button first."
                                        : "This prototype can record up to 32 tracks at once.";
        return false;
    }
    const auto createResult = takesDirectory.createDirectory();
    if (createResult.failed())
    {
        impl->error = "Cannot create the Takes folder: " + createResult.getErrorMessage();
        return false;
    }

    try
    {
        impl->preparePlayback(startSeconds);
        impl->beforeRecord = impl->model;
        LivePeak stale;while(impl->live.pop(stale)){}
        impl->recordAnchor.store(impl->start);
        impl->recordEnd.store(impl->start);
        impl->capturedFrames = 0;
        impl->recordFrames.store(0);
        impl->delayFrames = static_cast<std::int64_t>(std::llround(juce::jlimit(0.0, 2000.0, compensationMs) * impl->runningRate / 1000.0));
        impl->captureTarget = 0;
        impl->maxCaptureFrames = std::min<std::int64_t>(static_cast<std::int64_t>(impl->runningRate * 30.0 * 60.0),
            captureMemoryBudget / (static_cast<std::int64_t>(impl->takes.size()) * static_cast<std::int64_t>(sizeof(float))));
        impl->totalCountIn = static_cast<std::int64_t>(std::llround(4.0 * juce::jlimit(0, 2, impl->model.countInBars) * impl->framesPerBeat));
        impl->countInRemaining.store(impl->totalCountIn);
        const auto capacity = std::max(65536, static_cast<int>(std::ceil(impl->runningRate * 2.0))) + 1;
        impl->ring.setSize(static_cast<int>(impl->takes.size()), capacity);
        impl->ring.clear();
        impl->fifo = std::make_unique<juce::AbstractFifo>(capacity);
        impl->captureFinished.store(false);
        impl->workerDone.store(false);
        impl->overflow.store(false);
        impl->deviceLost.store(false);
        impl->writeFailed.store(false);
        impl->limitReached.store(false);
        impl->writerError.clear();
        juce::WavAudioFormat format;
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
        for (auto& take : impl->takes)
        {
            const auto name = juce::File::createLegalFileName(impl->model.tracks[take.trackIndex].name);
            take.file = takesDirectory.getNonexistentChildFile(stamp + "_" + name + "_" + juce::Uuid().toString().substring(0, 8), ".wav");
            std::unique_ptr<juce::OutputStream> stream(take.file.createOutputStream());
            if (stream)
                take.writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(impl->runningRate).withNumChannels(1).withBitsPerSample(32)
                    .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            if (!take.writer)
            {
                impl->error = "Cannot write recordings to the chosen Takes folder. Check that the drive has free space.";
                impl->takes.clear();
                return false;
            }
        }
        impl->writerThread = std::thread([this] { impl->runWriter(); });
        impl->transport.store(impl->totalCountIn > 0 ? Transport::countIn : Transport::recording, std::memory_order_release);
        return true;
    }
    catch (const std::exception& e)
    {
        impl->error = "Could not prepare recording: " + juce::String(e.what());
        impl->takes.clear();
        return false;
    }
}

void SessionEngine::stop() noexcept { impl->stopRequested.store(true); }
void SessionEngine::seek(double seconds) noexcept
{
    if (!isBusy())
        impl->displayedPosition.store(std::max(0.0, seconds));
}
bool SessionEngine::isPlaying() const noexcept
{
    const auto mode = impl->transport.load(std::memory_order_acquire);
    return mode == Transport::playing || mode == Transport::recording || mode == Transport::countIn;
}
bool SessionEngine::isRecording() const noexcept
{
    const auto mode = impl->transport.load(std::memory_order_acquire);
    return mode == Transport::recording || mode == Transport::countIn || mode == Transport::draining;
}
bool SessionEngine::isBusy() const noexcept { return impl->transport.load(std::memory_order_acquire) != Transport::idle; }
bool SessionEngine::isCountingIn() const noexcept { return impl->transport.load() == Transport::countIn; }
int SessionEngine::countInBeatsRemaining() const noexcept
{
    return static_cast<int>(std::ceil(static_cast<double>(impl->countInRemaining.load()) / impl->framesPerBeat));
}
double SessionEngine::position() const noexcept { return impl->displayedPosition.load(); }
double SessionEngine::recordingStart() const noexcept { return impl->recordAnchor.load(); }
double SessionEngine::recordingEnd() const noexcept { return impl->recordEnd.load(); }
double SessionEngine::sampleRate() const noexcept { return impl->rate.load(); }
float SessionEngine::inputPeak(int channel) const noexcept
{
    return channel >= 0 && channel < maxInputs ? impl->peaks[static_cast<std::size_t>(channel)].load() : 0.0f;
}
float SessionEngine::outputPeak() const noexcept { return impl->peakOut.load(); }
int SessionEngine::inputChannelCount() const noexcept { return impl->physicalInputs.load(); }
double SessionEngine::duration() const noexcept
{
    double end = 0.0;
    for (const auto& track : impl->model.tracks)
        for (const auto& clip : track.clips)
            end = std::max(end, clip.startSeconds + clip.lengthSeconds);
    return end;
}
juce::String SessionEngine::lastError() const { return impl->error; }
void SessionEngine::clearError() { impl->error.clear(); }
bool SessionEngine::canUndo() const noexcept { return !isBusy() && impl->undoAvailable; }
bool SessionEngine::undo()
{
    if (!canUndo())
        return false;
    impl->model = impl->undoSession;
    impl->undoAvailable = false;
    return true;
}

void SessionEngine::insertPunch(Track& track, const Clip& replacement)
{
    if (replacement.lengthSeconds <= 0.0)
        return;
    const auto begin = replacement.startSeconds;
    const auto end = begin + replacement.lengthSeconds;
    std::vector<Clip> result;
    result.reserve(track.clips.size() + 2);
    for (const auto& original : track.clips)
    {
        const auto originalEnd = original.startSeconds + original.lengthSeconds;
        if (originalEnd <= begin || original.startSeconds >= end)
        {
            result.push_back(original);
            continue;
        }
        if (original.startSeconds < begin)
        {
            auto left = original;
            left.lengthSeconds = begin - original.startSeconds;
            result.push_back(std::move(left));
        }
        if (originalEnd > end)
        {
            auto right = original;
            right.id = juce::Uuid().toString();
            right.sourceOffsetSeconds += end - original.startSeconds;
            right.startSeconds = end;
            right.lengthSeconds = originalEnd - end;
            result.push_back(std::move(right));
        }
    }
    result.push_back(replacement);
    std::stable_sort(result.begin(), result.end(), [] (const Clip& a, const Clip& b) { return a.startSeconds < b.startSeconds; });
    track.clips = std::move(result);
}

bool SessionEngine::readLivePeak(LivePeak& p) {return impl->live.pop(p);}
void SessionEngine::setDefaultInput(int ch) {if(!isBusy())impl->defaultInput=ch;}
void SessionEngine::setTunerInput(int channel) {impl->analysis.setInput(channel);}
bool SessionEngine::pollPitch(PitchResult& result) {return impl->analysis.poll(result);}

bool SessionEngine::poll()
{
    if(impl->driverErrorState.load(std::memory_order_acquire)==2)
    {impl->error="Audio driver error: "+juce::String::fromUTF8(impl->driverError.data());logEvent("ERROR "+impl->error);impl->driverErrorState.store(0,std::memory_order_release);}
    if (impl->transport.load(std::memory_order_acquire) != Transport::finalizing
        || !impl->workerDone.load(std::memory_order_acquire))
        return false;
    if (impl->writerThread.joinable())
        impl->writerThread.join();
    bool changed = false;
    if (impl->writerError.isNotEmpty())
        impl->error = impl->writerError;
    else if (impl->recordFrames.load() > 0)
    {
        auto length = impl->recordFrames.load();
        for (const auto& take : impl->takes)
            length = std::min<std::int64_t>(length, take.audio ? take.audio->getNumSamples() - impl->delayFrames : 0);
        if (length > 0)
        {
            impl->undoSession = impl->beforeRecord;
            impl->undoAvailable = true;
            for (const auto& take : impl->takes)
            {
                Clip clip;
                clip.id = juce::Uuid().toString();
                clip.file = take.file;
                clip.startSeconds = impl->recordAnchor.load();
                clip.sourceOffsetSeconds = static_cast<double>(impl->delayFrames) / impl->runningRate;
                clip.lengthSeconds = static_cast<double>(length) / impl->runningRate;
                clip.sampleRate = impl->runningRate;
                clip.audio = take.audio;
                insertPunch(impl->model.tracks[take.trackIndex], clip);
            }
            changed = true;
        }
    }
    if (impl->writerError.isEmpty())
    {
        if (impl->overflow.load())
            impl->error = changed ? "Recording stopped because the drive fell behind. The completed part of this take and all earlier audio have been kept."
                                  : "Recording stopped because the drive fell behind before a usable take was captured. Your earlier recordings are unchanged.";
        else if (impl->deviceLost.load())
            impl->error = changed ? "The audio device disconnected. The completed part of the take has been kept; its unfinished tail was trimmed."
                                  : "The audio device disconnected before a usable take was captured. Your earlier recordings are unchanged.";
        else if (impl->limitReached.load())
            impl->error = "This take reached the prototype's recording memory limit and was saved. You can start another take.";
    }
    impl->takes.clear();
    impl->fifo.reset();
    impl->ring.setSize(0, 0);
    impl->displayedPosition.store(impl->recordAnchor.load());
    impl->transport.store(Transport::idle, std::memory_order_release);
    return changed;
}

void SessionEngine::prepareForDevice(double sampleRate, int, int numInputs)
{
    impl->interrupted();
    impl->rate.store(sampleRate);
    impl->analysis.prepare(sampleRate);
    impl->physicalInputs.store(std::min(maxInputs, numInputs));
    impl->physicalToCallback.fill(-1);
    for (int i = 0; i < std::min(maxInputs, numInputs); ++i)
        impl->physicalToCallback[static_cast<std::size_t>(i)] = i;
    for (auto& peak : impl->peaks)
        peak.store(0.0f);
}

void SessionEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    prepareForDevice(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(), device->getInputChannelNames().size());
    impl->physicalToCallback.fill(-1);
    const auto active = device->getActiveInputChannels();
    int callbackChannel = 0;
    for (int physical = 0; physical < maxInputs; ++physical)
        if (active[physical])
            impl->physicalToCallback[static_cast<std::size_t>(physical)] = callbackChannel++;
}
void SessionEngine::audioDeviceStopped()
{
    impl->interrupted();
    impl->rate.store(0.0);
}
void SessionEngine::audioDeviceError(const juce::String& error)
{
    int expected=0;if(impl->driverErrorState.compare_exchange_strong(expected,1))
    {error.copyToUTF8(impl->driverError.data(),impl->driverError.size());impl->driverErrorState.store(2,std::memory_order_release);}
    // This may arrive on any thread: let the next callback stop gracefully.
    // audioDeviceStopped handles the case where no further callback arrives.
    impl->deviceLost.store(true);
    impl->stopRequested.store(true);
    impl->rate.store(0);
}

void SessionEngine::audioDeviceIOCallbackWithContext(const float* const* inputs, int inputCount,
    float* const* outputs, int outputCount, int sampleCount, const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < outputCount; ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputs[channel], sampleCount);
    for (int physical = 0; physical < impl->physicalInputs.load(); ++physical)
    {
        const auto input = impl->physicalToCallback[static_cast<std::size_t>(physical)];
        impl->analysis.capture(input>=0&&input<inputCount?inputs[input]:nullptr,sampleCount,physical);
        const auto range = input >= 0 && input < inputCount && inputs[input] != nullptr
            ? juce::FloatVectorOperations::findMinAndMax(inputs[input], sampleCount) : juce::Range<float>();
        auto& meter = impl->peaks[static_cast<std::size_t>(physical)];
        meter.store(std::max(meter.load() * 0.94f, std::max(std::abs(range.getEnd()), std::abs(range.getStart()))));
    }
    if (impl->stopRequested.exchange(false))
        impl->stopAtBoundary();

    int offset = 0;
    while (offset < sampleCount)
    {
        const auto mode = impl->transport.load(std::memory_order_acquire);
        if (mode == Transport::idle || mode == Transport::finalizing)
            break;
        auto count = sampleCount - offset;
        if (mode == Transport::countIn)
        {
            const auto remaining = impl->countInRemaining.load();
            count = static_cast<int>(std::min<std::int64_t>(count, remaining));
            const auto elapsed = impl->totalCountIn - remaining;
            for (int i = 0; i < count; ++i)
            {
                const auto click = metronomeSample(static_cast<double>(elapsed + i), impl->framesPerBeat, impl->runningRate);
                for (int channel = 0; channel < outputCount; ++channel)
                    if (outputs[channel] != nullptr)
                        outputs[channel][offset + i] = click;
            }
            impl->countInRemaining.store(remaining - count);
            if (remaining == count)
                impl->transport.store(Transport::recording, std::memory_order_release);
        }
        else if (mode == Transport::draining)
        {
            count = static_cast<int>(std::min<std::int64_t>(count, impl->captureTarget - impl->capturedFrames));
            if (count > 0 && !impl->capture(inputs, inputCount, offset, count))
                break;
            if (impl->capturedFrames >= impl->captureTarget)
                impl->finishCapture();
        }
        else
        {
            if (mode == Transport::recording)
            {
                count = static_cast<int>(std::min<std::int64_t>(count, impl->maxCaptureFrames - impl->delayFrames - impl->timelineFrames));
                if (count <= 0)
                {
                    impl->limitReached.store(true);
                    impl->stopAtBoundary();
                    continue;
                }
                if (!impl->capture(inputs, inputCount, offset, count))
                    break;
            }
            impl->render(outputs, outputCount, offset, count, mode == Transport::recording);
            impl->timelineFrames += count;
            impl->displayedPosition.store(impl->start + static_cast<double>(impl->timelineFrames) / impl->runningRate);
            if (mode == Transport::recording)
                impl->recordEnd.store(impl->displayedPosition.load());
        }
        offset += count;
    }
    float peak = 0.0f;
    for (int channel = 0; channel < outputCount; ++channel)
        if (outputs[channel] != nullptr)
            for (int i = 0; i < sampleCount; ++i)
            {
                peak = std::max(peak, std::abs(outputs[channel][i]));
                outputs[channel][i] = juce::jlimit(-1.0f, 1.0f, outputs[channel][i]);
            }
    impl->peakOut.store(std::max(impl->peakOut.load() * 0.94f, peak));
}
}
