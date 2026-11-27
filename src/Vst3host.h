#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <public.sdk/source/vst/hosting/plugprovider.h>

#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>
#include <pluginterfaces/vst/vstspeaker.h>
#include <pluginterfaces/vst/vsttypes.h>

// Minimal in-memory IBStream used to save / restore plugin state.
// (Own implementation: the SDK's MemoryStream is not part of the libraries we link.)
class ByteStream : public Steinberg::IBStream
{
public:
    std::vector<unsigned char> bytes;
    Steinberg::int64 position = 0;

    ByteStream() = default;

    explicit ByteStream(const std::vector<unsigned char>& source)
        : bytes(source)
    {
    }

    Steinberg::tresult PLUGIN_API read(
        void* buffer,
        Steinberg::int32 numBytes,
        Steinberg::int32* numBytesRead) override
    {
        if (!buffer || numBytes < 0)
            return Steinberg::kInvalidArgument;

        const Steinberg::int64 size = static_cast<Steinberg::int64>(bytes.size());
        Steinberg::int64 count = std::min<Steinberg::int64>(numBytes, size - position);
        if (count < 0)
            count = 0;

        if (count > 0)
            std::memcpy(buffer, bytes.data() + position, static_cast<size_t>(count));

        position += count;

        if (numBytesRead)
            *numBytesRead = static_cast<Steinberg::int32>(count);

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API write(
        void* buffer,
        Steinberg::int32 numBytes,
        Steinberg::int32* numBytesWritten) override
    {
        if (!buffer || numBytes < 0)
            return Steinberg::kInvalidArgument;

        const size_t end = static_cast<size_t>(position) + static_cast<size_t>(numBytes);
        if (end > bytes.size())
            bytes.resize(end, 0);

        if (numBytes > 0)
            std::memcpy(bytes.data() + position, buffer, static_cast<size_t>(numBytes));

        position += numBytes;

        if (numBytesWritten)
            *numBytesWritten = numBytes;

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API seek(
        Steinberg::int64 pos,
        Steinberg::int32 mode,
        Steinberg::int64* result) override
    {
        Steinberg::int64 target = 0;

        switch (mode)
        {
        case Steinberg::IBStream::kIBSeekSet: target = pos; break;
        case Steinberg::IBStream::kIBSeekCur: target = position + pos; break;
        case Steinberg::IBStream::kIBSeekEnd: target = static_cast<Steinberg::int64>(bytes.size()) + pos; break;
        default: return Steinberg::kInvalidArgument;
        }

        if (target < 0)
            return Steinberg::kInvalidArgument;

        position = target;

        if (result)
            *result = position;

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API tell(Steinberg::int64* pos) override
    {
        if (!pos)
            return Steinberg::kInvalidArgument;

        *pos = position;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API queryInterface(
        const Steinberg::TUID iid,
        void** obj) override
    {
        if (!obj)
            return Steinberg::kInvalidArgument;

        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IBStream::iid))
        {
            *obj = this;
            addRef();
            return Steinberg::kResultOk;
        }

        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    // lives on the stack of the caller, so reference counting is a no-op
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

struct VstNote
{
    int pitch = 60;
    double startSec = 0.0;
    double endSec = 0.0;
    float velocity = 0.8f;
};

inline std::vector<std::string> ScanVst3Folders()
{
    namespace fs = std::filesystem;

    std::vector<std::string> found;

    const char* roots[] =
    {
        "C:\\Program Files\\Common Files\\VST3",
        "C:\\Program Files (x86)\\Common Files\\VST3"
    };

    for (const char* root : roots)
    {
        std::error_code ec;

        if (!fs::exists(root, ec))
            continue;

        fs::recursive_directory_iterator it(
            root,
            fs::directory_options::skip_permission_denied,
            ec
        );

        fs::recursive_directory_iterator end;

        while (!ec && it != end)
        {
            const auto current = it->path();

            if (current.extension() == ".vst3")
            {
                found.push_back(current.string());

                if (it->is_directory(ec))
                    it.disable_recursion_pending();
            }

            it.increment(ec);
        }
    }

    std::sort(found.begin(), found.end());

    return found;
}

class Vst3Plugin
{
public:
    std::string name;
    std::string path;
    std::string lastError;

    bool instrument = false;
    HWND hwnd = nullptr;

    Vst3Plugin()
    {
        frame.owner = this;
    }

    ~Vst3Plugin()
    {
        shutdown();
    }

    Vst3Plugin(const Vst3Plugin&) = delete;
    Vst3Plugin& operator=(const Vst3Plugin&) = delete;

    bool load(
        const std::string& pluginPath,
        double sr,
        int block)
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        shutdown();

        lastError.clear();

        if (!std::isfinite(sr) || sr < 8000.0 || sr > 384000.0)
        {
            lastError = "Invalid sample rate.";
            return false;
        }

        if (pluginPath.empty())
        {
            lastError = "Plugin path is empty.";
            return false;
        }

        sampleRate = sr;
        maxBlock = std::clamp(block, 64, 16384);
        path = pluginPath;

        initHost();

        std::string err;

        module = VST3::Hosting::Module::create(pluginPath, err);

        if (!module)
        {
            lastError = "Module load failed: " + err;
            return false;
        }

        auto factory = module->getFactory();

        std::optional<VST3::Hosting::ClassInfo> chosen;

        for (const auto& ci : factory.classInfos())
        {
            if (ci.category() == kVstAudioEffectClass)
            {
                chosen = ci;
                break;
            }
        }

        if (!chosen)
        {
            lastError = "No audio effect class in plugin.";
            module.reset();
            return false;
        }

        name = chosen->name();

        provider = owned(
            new PlugProvider(factory, *chosen, true)
        );

        if (!provider->initialize())
        {
            lastError = "Plugin initialize failed.";
            provider = nullptr;
            module.reset();
            return false;
        }

        component = provider->getComponentPtr();
        controller = provider->getControllerPtr();

        if (!component)
        {
            lastError = "No component.";
            shutdown();
            return false;
        }

        if (component->queryInterface(
            IAudioProcessor::iid,
            reinterpret_cast<void**>(&proc)) != kResultOk ||
            !proc)
        {
            lastError = "Plugin has no IAudioProcessor.";
            proc = nullptr;
            shutdown();
            return false;
        }

        if (controller)
            controller->setComponentHandler(&handler);

        const int32 nAudioIn =
            component->getBusCount(kAudio, kInput);

        const int32 nAudioOut =
            component->getBusCount(kAudio, kOutput);

        const int32 nEventIn =
            component->getBusCount(kEvent, kInput);

        if (nAudioIn < 0 ||
            nAudioOut < 0 ||
            nEventIn < 0 ||
            nAudioIn > 16 ||
            nAudioOut > 16)
        {
            lastError = "Unsupported VST3 bus count.";
            shutdown();
            return false;
        }

        numInBuses = nAudioIn;
        numOutBuses = nAudioOut;
        numEventInBuses = nEventIn;

        std::vector<SpeakerArrangement> ins(
            static_cast<size_t>(nAudioIn),
            SpeakerArr::kStereo
        );

        std::vector<SpeakerArrangement> outs(
            static_cast<size_t>(nAudioOut),
            SpeakerArr::kStereo
        );

        const tresult arrangementResult =
            proc->setBusArrangements(
                ins.empty() ? nullptr : ins.data(),
                nAudioIn,
                outs.empty() ? nullptr : outs.data(),
                nAudioOut
            );

        if (arrangementResult != kResultOk)
        {
            lastError = "setBusArrangements failed.";
            shutdown();
            return false;
        }

        for (int32 i = 0; i < nAudioIn; ++i)
        {
            if (component->activateBus(
                kAudio,
                kInput,
                i,
                i == 0) != kResultOk)
            {
                lastError = "Could not activate input bus.";
                shutdown();
                return false;
            }
        }

        for (int32 i = 0; i < nAudioOut; ++i)
        {
            if (component->activateBus(
                kAudio,
                kOutput,
                i,
                i == 0) != kResultOk)
            {
                lastError = "Could not activate output bus.";
                shutdown();
                return false;
            }
        }

        if (nEventIn > 0)
        {
            if (component->activateBus(
                kEvent,
                kInput,
                0,
                true) != kResultOk)
            {
                lastError = "Could not activate event input bus.";
                shutdown();
                return false;
            }
        }

        inCh = 0;
        outCh = 2;

        SpeakerArrangement arr = 0;

        if (nAudioIn > 0 &&
            proc->getBusArrangement(
                kInput,
                0,
                arr) == kResultOk)
        {
            inCh = static_cast<int>(
                SpeakerArr::getChannelCount(arr)
                );

            if (inCh < 1 || inCh > 2)
            {
                lastError = "Unsupported input channel count.";
                shutdown();
                return false;
            }
        }

        if (nAudioOut > 0 &&
            proc->getBusArrangement(
                kOutput,
                0,
                arr) == kResultOk)
        {
            outCh = static_cast<int>(
                SpeakerArr::getChannelCount(arr)
                );

            if (outCh < 1 || outCh > 2)
            {
                lastError = "Unsupported output channel count.";
                shutdown();
                return false;
            }
        }

        instrument =
            chosen->subCategoriesString().find("Instrument")
            != std::string::npos
            || nAudioIn == 0;

        ProcessSetup setup{};

        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = maxBlock;
        setup.sampleRate = sampleRate;

        if (proc->setupProcessing(setup) != kResultOk)
        {
            lastError = "setupProcessing failed.";
            shutdown();
            return false;
        }

        if (component->setActive(true) != kResultOk)
        {
            lastError = "setActive failed.";
            shutdown();
            return false;
        }

        componentActive = true;

        if (proc->setProcessing(true) != kResultOk)
        {
            lastError = "setProcessing failed.";
            shutdown();
            return false;
        }

        processing = true;

        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign(
                static_cast<size_t>(maxBlock),
                0.0f
            );

            outBuf[c].assign(
                static_cast<size_t>(maxBlock),
                0.0f
            );
        }

        effectScratch.assign(
            static_cast<size_t>(maxBlock) * 2,
            0.0f
        );

        for (auto& b : inBuses)
            b = AudioBusBuffers{};

        for (auto& b : outBuses)
            b = AudioBusBuffers{};

        paramChanges.setMaxParameters(256);

        active = true;
        lastError.clear();

        return true;
    }

    bool isLoaded() const
    {
        return active && proc != nullptr;
    }

    int getMaxBlockSize() const
    {
        return maxBlock;
    }

    double getSampleRate() const
    {
        return sampleRate;
    }

    int getInputChannels() const
    {
        return inCh;
    }

    int getOutputChannels() const
    {
        return outCh;
    }

    // -------------------------------------------------
    // MIDI
    // -------------------------------------------------

    void beginRealtimeBlock()
    {
        events.clear();
    }

    bool queueNoteOn(
        int pitch,
        float velocity,
        int sampleOffset = 0,
        int channel = 0)
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        if (!active || !instrument || numEventInBuses <= 0)
            return false;

        Event e{};

        e.busIndex = 0;
        e.sampleOffset = std::clamp(
            sampleOffset,
            0,
            maxBlock - 1
        );

        e.ppqPosition = 0.0;
        e.flags = 0;
        e.type = Event::kNoteOnEvent;

        e.noteOn.channel =
            static_cast<int16>(std::clamp(channel, 0, 15));

        e.noteOn.pitch =
            static_cast<int16>(std::clamp(pitch, 0, 127));

        e.noteOn.tuning = 0.0f;
        e.noteOn.velocity = std::clamp(velocity, 0.0f, 1.0f);
        e.noteOn.length = 0;
        e.noteOn.noteId = -1;

        return events.addEvent(e) == kResultOk;
    }

    bool queueNoteOff(
        int pitch,
        int sampleOffset = 0,
        int channel = 0,
        float velocity = 0.0f)
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        if (!active || !instrument || numEventInBuses <= 0)
            return false;

        Event e{};

        e.busIndex = 0;
        e.sampleOffset = std::clamp(
            sampleOffset,
            0,
            maxBlock - 1
        );

        e.ppqPosition = 0.0;
        e.flags = 0;
        e.type = Event::kNoteOffEvent;

        e.noteOff.channel =
            static_cast<int16>(std::clamp(channel, 0, 15));

        e.noteOff.pitch =
            static_cast<int16>(std::clamp(pitch, 0, 127));

        e.noteOff.velocity = std::clamp(velocity, 0.0f, 1.0f);
        e.noteOff.noteId = -1;
        e.noteOff.tuning = 0.0f;

        return events.addEvent(e) == kResultOk;
    }

    void allNotesOff()
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            queueNoteOff(pitch);
    }

    // Flush the plugin: silences hanging notes and clears reverb/delay tails.
    // Call from the same thread that owns the audio lock (never while process() is running).
    void resetState()
    {
        using namespace Steinberg;

        if (!active || !proc || !component)
            return;

        events.clear();

        if (processing)
        {
            proc->setProcessing(false);
            processing = false;
        }

        if (componentActive)
        {
            component->setActive(false);
            componentActive = false;
        }

        if (component->setActive(true) == kResultOk)
        {
            componentActive = true;

            if (proc->setProcessing(true) == kResultOk)
                processing = true;
        }
    }

    // -------------------------------------------------
    // VST3 processing
    // -------------------------------------------------

    bool processRealtime(
        const float* inputStereo,
        float* outputStereo,
        int frames,
        double bpm,
        long long projectSamplePosition,
        bool transportPlaying = true)
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        if (!active || !proc)
            return false;

        if (!outputStereo ||
            frames <= 0 ||
            frames > maxBlock)
        {
            return false;
        }

        if (numInBuses > 16 || numOutBuses > 16)
            return false;

        if (numInBuses > 0 &&
            !inputStereo &&
            !instrument)
        {
            return false;
        }

        // Input
        if (numInBuses > 0)
        {
            if (inputStereo)
            {
                for (int i = 0; i < frames; ++i)
                {
                    inBuf[0][i] = inputStereo[i * 2];
                    inBuf[1][i] = inputStereo[i * 2 + 1];
                }
            }
            else
            {
                std::fill_n(inBuf[0].data(), frames, 0.0f);
                std::fill_n(inBuf[1].data(), frames, 0.0f);
            }
        }

        // Clear output
        std::fill_n(outBuf[0].data(), frames, 0.0f);
        std::fill_n(outBuf[1].data(), frames, 0.0f);

        float* inPtrs[2] =
        {
            inBuf[0].data(),
            inBuf[1].data()
        };

        float* outPtrs[2] =
        {
            outBuf[0].data(),
            outBuf[1].data()
        };

        for (auto& b : inBuses)
            b = AudioBusBuffers{};

        for (auto& b : outBuses)
            b = AudioBusBuffers{};

        if (numInBuses > 0)
        {
            inBuses[0].numChannels = inCh;
            inBuses[0].silenceFlags = 0;
            inBuses[0].channelBuffers32 = inPtrs;
        }

        if (numOutBuses > 0)
        {
            outBuses[0].numChannels = outCh;
            outBuses[0].silenceFlags = 0;
            outBuses[0].channelBuffers32 = outPtrs;
        }

        // Process context
        ProcessContext ctx{};

        ctx.state =
            ProcessContext::kTempoValid |
            ProcessContext::kTimeSigValid |
            ProcessContext::kProjectTimeMusicValid;
        if (transportPlaying)
            ctx.state |= ProcessContext::kPlaying;

        ctx.sampleRate = sampleRate;
        ctx.projectTimeSamples = projectSamplePosition;
        ctx.tempo = std::clamp(
            std::isfinite(bpm) ? bpm : 120.0,
            1.0,
            999.0
        );

        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;

        ctx.projectTimeMusic =
            static_cast<double>(projectSamplePosition)
            / sampleRate
            * ctx.tempo
            / 60.0;

        // Process data
        ProcessData d{};

        d.processMode = kRealtime;
        d.symbolicSampleSize = kSample32;
        d.numSamples = frames;

        d.numInputs = numInBuses;
        d.numOutputs = numOutBuses;

        d.inputs = numInBuses > 0
            ? inBuses.data()
            : nullptr;

        d.outputs = numOutBuses > 0
            ? outBuses.data()
            : nullptr;

        d.inputEvents =
            numEventInBuses > 0 ? &events : nullptr;

        // parameter edits coming from the plugin GUI
        paramChanges.clearQueue();
        {
            std::lock_guard<std::mutex> lock(handler.mtx);
            for (const auto& p : handler.pending)
            {
                int32 index = 0;
                if (IParamValueQueue* q = paramChanges.addParameterData(p.first, index))
                {
                    int32 pointIndex = 0;
                    q->addPoint(0, p.second, pointIndex);
                }
            }
            handler.pending.clear();
        }

        d.inputParameterChanges = &paramChanges;
        d.processContext = &ctx;

        if (proc->process(d) != kResultOk)
        {
            std::fill_n(outputStereo, frames * 2, 0.0f);
            return false;
        }

        if (numOutBuses <= 0)
        {
            std::fill_n(outputStereo, frames * 2, 0.0f);
            return true;
        }

        // Convert output to stereo
        for (int i = 0; i < frames; ++i)
        {
            const float left = outBuf[0][i];

            const float right =
                outCh > 1 ? outBuf[1][i] : left;

            outputStereo[i * 2] = left;
            outputStereo[i * 2 + 1] = right;
        }

        return true;
    }

    bool processRealtimeInstrument(
        float* outputStereo,
        int frames,
        double bpm,
        long long projectSamplePosition)
    {
        if (!instrument)
            return false;

        return processRealtime(
            nullptr,
            outputStereo,
            frames,
            bpm,
            projectSamplePosition
        );
    }

    bool processRealtimeEffect(
        float* samples,
        int frames,
        double bpm,
        long long projectSamplePosition,
        bool transportPlaying = true)
    {
        if (instrument || !samples)
            return false;

        if (frames <= 0 || frames > maxBlock)
            return false;

        if (effectScratch.size() <
            static_cast<size_t>(frames) * 2)
        {
            return false;
        }

        if (!processRealtime(
            samples,
            effectScratch.data(),
            frames,
            bpm,
            projectSamplePosition,
            transportPlaying))
        {
            return false;
        }

        std::memcpy(
            samples,
            effectScratch.data(),
            sizeof(float) * static_cast<size_t>(frames) * 2
        );

        return true;
    }

    // -------------------------------------------------
    // Offline effect
    // -------------------------------------------------

    void processEffect(
        std::vector<float>& samples,
        double bpm)
    {
        if (!active || instrument || samples.empty())
            return;

        if (samples.size() % 2 != 0)
        {
            lastError = "Invalid stereo sample buffer.";
            return;
        }

        const size_t totalFrames = samples.size() / 2;

        std::vector<float> input(
            static_cast<size_t>(maxBlock) * 2,
            0.0f
        );

        std::vector<float> output(
            static_cast<size_t>(maxBlock) * 2,
            0.0f
        );

        for (size_t pos = 0; pos < totalFrames; pos += maxBlock)
        {
            const int frames = static_cast<int>(
                std::min<size_t>(
                    static_cast<size_t>(maxBlock),
                    totalFrames - pos
                )
                );

            std::memcpy(
                input.data(),
                samples.data() + pos * 2,
                sizeof(float) * static_cast<size_t>(frames) * 2
            );

            events.clear();

            if (!processRealtime(
                input.data(),
                output.data(),
                frames,
                bpm,
                static_cast<long long>(pos)))
            {
                lastError = "VST3 effect processing failed.";
                return;
            }

            std::memcpy(
                samples.data() + pos * 2,
                output.data(),
                sizeof(float) * static_cast<size_t>(frames) * 2
            );
        }

        lastError.clear();
    }

    // -------------------------------------------------
    // Offline instrument rendering
    // -------------------------------------------------

    void renderInstrument(
        const std::vector<VstNote>& notes,
        double bpm,
        double totalSeconds,
        std::vector<float>& out)
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        out.clear();

        if (!active || !proc || !instrument)
            return;

        if (!std::isfinite(totalSeconds) ||
            totalSeconds <= 0.0 ||
            !std::isfinite(bpm))
        {
            lastError = "Invalid render settings.";
            return;
        }

        if (numEventInBuses <= 0)
        {
            lastError = "Instrument has no MIDI input bus.";
            return;
        }

        const long double frameCount =
            std::ceil(
                (static_cast<long double>(totalSeconds) + 1.0L)
                * sampleRate
            );

        if (frameCount <= 0 ||
            frameCount >
            static_cast<long double>(
                std::numeric_limits<size_t>::max() / 2
                ) ||
            frameCount >
            static_cast<long double>(
                std::numeric_limits<long long>::max()
                ))
        {
            lastError = "Render duration is too large.";
            return;
        }

        const long long total =
            static_cast<long long>(frameCount);

        out.assign(
            static_cast<size_t>(total) * 2,
            0.0f
        );

        struct Ev
        {
            long long frame;
            bool on;
            int pitch;
            float velocity;
        };

        std::vector<Ev> evs;
        evs.reserve(notes.size() * 2);

        for (const auto& note : notes)
        {
            if (!std::isfinite(note.startSec) ||
                !std::isfinite(note.endSec) ||
                !std::isfinite(note.velocity))
            {
                continue;
            }

            if (note.pitch < 0 || note.pitch > 127)
                continue;

            if (note.startSec < 0.0 ||
                note.endSec <= note.startSec)
            {
                continue;
            }

            const long double start =
                static_cast<long double>(note.startSec)
                * sampleRate;

            const long double end =
                static_cast<long double>(note.endSec)
                * sampleRate;

            if (start >= total)
                continue;

            if (start >
                static_cast<long double>(
                    std::numeric_limits<long long>::max()
                    ) ||
                end >
                static_cast<long double>(
                    std::numeric_limits<long long>::max()
                    ))
            {
                continue;
            }

            const long long startFrame =
                static_cast<long long>(start);

            const long long endFrame =
                static_cast<long long>(end);

            evs.push_back({
                startFrame,
                true,
                note.pitch,
                std::clamp(note.velocity, 0.0f, 1.0f)
                });

            evs.push_back({
                std::min(endFrame, total - 1),
                false,
                note.pitch,
                0.0f
                });
        }

        std::stable_sort(
            evs.begin(),
            evs.end(),
            [](const Ev& a, const Ev& b)
            {
                if (a.frame != b.frame)
                    return a.frame < b.frame;

                return !a.on && b.on;
            }
        );

        std::vector<float> rendered(
            static_cast<size_t>(maxBlock) * 2,
            0.0f
        );

        size_t ei = 0;

        for (long long pos = 0; pos < total; pos += maxBlock)
        {
            const int frames = static_cast<int>(
                std::min<long long>(
                    maxBlock,
                    total - pos
                )
                );

            events.clear();

            while (ei < evs.size() &&
                evs[ei].frame < pos + frames)
            {
                const int offset =
                    static_cast<int>(evs[ei].frame - pos);

                Event e{};

                e.busIndex = 0;
                e.sampleOffset = offset;
                e.ppqPosition = 0.0;
                e.flags = 0;

                if (evs[ei].on)
                {
                    e.type = Event::kNoteOnEvent;

                    e.noteOn.channel = 0;
                    e.noteOn.pitch =
                        static_cast<int16>(evs[ei].pitch);

                    e.noteOn.tuning = 0.0f;
                    e.noteOn.velocity = evs[ei].velocity;
                    e.noteOn.length = 0;
                    e.noteOn.noteId = -1;
                }
                else
                {
                    e.type = Event::kNoteOffEvent;

                    e.noteOff.channel = 0;
                    e.noteOff.pitch =
                        static_cast<int16>(evs[ei].pitch);

                    e.noteOff.velocity = 0.0f;
                    e.noteOff.noteId = -1;
                    e.noteOff.tuning = 0.0f;
                }

                if (events.addEvent(e) != kResultOk)
                {
                    lastError = "Could not add MIDI event.";
                    events.clear();
                    out.clear();
                    return;
                }

                ++ei;
            }

            std::fill_n(
                rendered.data(),
                static_cast<size_t>(frames) * 2,
                0.0f
            );

            if (!processRealtime(
                nullptr,
                rendered.data(),
                frames,
                bpm,
                pos))
            {
                lastError = "VST3 instrument processing failed.";
                events.clear();
                out.clear();
                return;
            }

            std::memcpy(
                out.data() + static_cast<size_t>(pos) * 2,
                rendered.data(),
                sizeof(float) * static_cast<size_t>(frames) * 2
            );
        }

        events.clear();

        for (float& sample : out)
        {
            if (!std::isfinite(sample))
                sample = 0.0f;

            sample = std::clamp(sample, -1.0f, 1.0f);
        }

        lastError.clear();
    }

    // -------------------------------------------------
    // VST3 Editor
    // -------------------------------------------------

    bool openEditor(HWND owner)
    {
        if (hwnd)
        {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return true;
        }

        if (!controller)
        {
            lastError = "Plugin has no editor controller.";
            return false;
        }

        view = Steinberg::owned(
            controller->createView(
                Steinberg::Vst::ViewType::kEditor
            )
        );

        if (!view)
        {
            lastError = "Plugin has no editor.";
            return false;
        }

        if (view->isPlatformTypeSupported(
            Steinberg::kPlatformTypeHWND) !=
            Steinberg::kResultTrue)
        {
            lastError = "Editor does not support HWND.";
            view = nullptr;
            return false;
        }

        registerWindowClass();

        Steinberg::ViewRect r{};

        if (view->getSize(&r) != Steinberg::kResultTrue)
        {
            lastError = "Could not get editor size.";
            view = nullptr;
            return false;
        }

        RECT rc =
        {
            0,
            0,
            r.getWidth(),
            r.getHeight()
        };

        AdjustWindowRect(&rc, editorStyle, FALSE);

        hwnd = CreateWindowExA(
            0,
            "EasyMixVst3Editor",
            name.c_str(),
            editorStyle,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            owner,
            nullptr,
            GetModuleHandleA(nullptr),
            nullptr
        );

        if (!hwnd)
        {
            lastError = "Could not create editor window.";
            view = nullptr;
            return false;
        }

        SetWindowLongPtrA(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(this)
        );

        view->setFrame(&frame);

        if (view->attached(
            hwnd,
            Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk)
        {
            lastError = "Could not attach plugin editor.";
            view->setFrame(nullptr);
            view = nullptr;

            DestroyWindow(hwnd);
            hwnd = nullptr;

            return false;
        }

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        return true;
    }

    void closeEditor()
    {
        if (view)
        {
            view->removed();
            view->setFrame(nullptr);
            view = nullptr;
        }

        if (hwnd)
        {
            HWND h = hwnd;
            hwnd = nullptr;

            SetWindowLongPtrA(h, GWLP_USERDATA, 0);
            DestroyWindow(h);
        }
    }

    // -------------------------------------------------
    // Shutdown
    // -------------------------------------------------

    // -------------------------------------------------
    // Plugin state (saved inside the .emix project)
    // -------------------------------------------------

    // Returns the processor state as raw bytes (empty if unsupported).
    std::vector<unsigned char> saveState()
    {
        std::vector<unsigned char> bytes;

        if (!active || !component)
            return bytes;

        ByteStream stream;

        if (component->getState(&stream) == Steinberg::kResultOk &&
            !stream.bytes.empty() &&
            stream.bytes.size() < (256u * 1024u * 1024u))
        {
            bytes = std::move(stream.bytes);
        }

        return bytes;
    }

    // Restores a state produced by saveState(). Call right after load().
    bool loadState(const std::vector<unsigned char>& bytes)
    {
        if (!active || !component || bytes.empty())
            return false;

        {
            ByteStream stream(bytes);

            if (component->setState(&stream) != Steinberg::kResultOk)
                return false;
        }

        // keep the editor (controller) in sync with the processor
        if (controller)
        {
            ByteStream stream(bytes);

            controller->setComponentState(&stream);
        }

        return true;
    }

    void shutdown()
    {
        closeEditor();

        if (controller)
            controller->setComponentHandler(nullptr);

        if (proc)
        {
            if (processing)
            {
                proc->setProcessing(false);
                processing = false;
            }

            if (componentActive && component)
            {
                component->setActive(false);
                componentActive = false;
            }

            proc->release();
            proc = nullptr;
        }

        active = false;

        component = nullptr;
        controller = nullptr;
        provider = nullptr;
        module.reset();

        events.clear();

        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].clear();
            outBuf[c].clear();
        }

        effectScratch.clear();

        numInBuses = 0;
        numOutBuses = 0;
        numEventInBuses = 0;

        inCh = 0;
        outCh = 2;

        instrument = false;
        name.clear();
        path.clear();
    }

private:

    // -------------------------------------------------
    // Component Handler
    // -------------------------------------------------

    struct Handler : Steinberg::Vst::IComponentHandler
    {
        std::mutex mtx;

        Steinberg::tresult PLUGIN_API beginEdit(
            Steinberg::Vst::ParamID) override
        {
            return Steinberg::kResultOk;
        }

        // Edits made in the plugin GUI (knobs, sliders...). They arrive on the UI thread and
        // are handed to the processor with the next process() call.
        std::vector<std::pair<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>> pending;

        Steinberg::tresult PLUGIN_API performEdit(
            Steinberg::Vst::ParamID id,
            Steinberg::Vst::ParamValue value) override
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (pending.size() < 4096)
                pending.emplace_back(id, value);
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API endEdit(
            Steinberg::Vst::ParamID) override
        {
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API restartComponent(
            Steinberg::int32) override
        {
            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API queryInterface(
            const Steinberg::TUID iid,
            void** obj) override
        {
            if (!obj)
                return Steinberg::kInvalidArgument;

            if (Steinberg::FUnknownPrivate::iidEqual(
                iid,
                Steinberg::FUnknown::iid) ||
                Steinberg::FUnknownPrivate::iidEqual(
                    iid,
                    Steinberg::Vst::IComponentHandler::iid))
            {
                *obj = this;
                addRef();
                return Steinberg::kResultOk;
            }

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        Steinberg::uint32 PLUGIN_API addRef() override
        {
            return 1;
        }

        Steinberg::uint32 PLUGIN_API release() override
        {
            return 1;
        }
    };

    // -------------------------------------------------
    // Plug Frame
    // -------------------------------------------------

    struct Frame : Steinberg::IPlugFrame
    {
        Vst3Plugin* owner = nullptr;

        Steinberg::tresult PLUGIN_API resizeView(
            Steinberg::IPlugView* v,
            Steinberg::ViewRect* r) override
        {
            if (owner && owner->hwnd && r)
            {
                RECT rc =
                {
                    0,
                    0,
                    r->getWidth(),
                    r->getHeight()
                };

                AdjustWindowRect(
                    &rc,
                    owner->editorStyle,
                    FALSE
                );

                SetWindowPos(
                    owner->hwnd,
                    nullptr,
                    0,
                    0,
                    rc.right - rc.left,
                    rc.bottom - rc.top,
                    SWP_NOMOVE |
                    SWP_NOZORDER |
                    SWP_NOACTIVATE
                );

                if (v)
                    v->onSize(r);
            }

            return Steinberg::kResultOk;
        }

        Steinberg::tresult PLUGIN_API queryInterface(
            const Steinberg::TUID iid,
            void** obj) override
        {
            if (!obj)
                return Steinberg::kInvalidArgument;

            if (Steinberg::FUnknownPrivate::iidEqual(
                iid,
                Steinberg::FUnknown::iid) ||
                Steinberg::FUnknownPrivate::iidEqual(
                    iid,
                    Steinberg::IPlugFrame::iid))
            {
                *obj = this;
                addRef();
                return Steinberg::kResultOk;
            }

            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        Steinberg::uint32 PLUGIN_API addRef() override
        {
            return 1;
        }

        Steinberg::uint32 PLUGIN_API release() override
        {
            return 1;
        }
    };

    // -------------------------------------------------
    // Host initialization
    // -------------------------------------------------

    static void initHost()
    {
        static Steinberg::Vst::HostApplication app;
        static bool initialized = false;
        static std::mutex initMutex;

        std::lock_guard<std::mutex> lock(initMutex);

        if (!initialized)
        {
            Steinberg::Vst::PluginContextFactory::instance()
                .setPluginContext(&app);

            initialized = true;
        }
    }

    // -------------------------------------------------
    // Editor window
    // -------------------------------------------------

    static LRESULT CALLBACK EditorProc(
        HWND h,
        UINT msg,
        WPARAM w,
        LPARAM l)
    {
        if (msg == WM_CLOSE)
        {
            auto* self =
                reinterpret_cast<Vst3Plugin*>(
                    GetWindowLongPtrA(h, GWLP_USERDATA)
                    );

            if (self)
                self->closeEditor();
            else
                DestroyWindow(h);

            return 0;
        }

        return DefWindowProcA(h, msg, w, l);
    }

    static void registerWindowClass()
    {
        static bool registered = false;
        static std::mutex registrationMutex;

        std::lock_guard<std::mutex> lock(registrationMutex);

        if (registered)
            return;

        WNDCLASSA wc{};

        wc.lpfnWndProc = EditorProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "EasyMixVst3Editor";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        if (RegisterClassA(&wc) ||
            GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        {
            registered = true;
        }
    }

private:

    VST3::Hosting::Module::Ptr module;

    Steinberg::IPtr<Steinberg::Vst::PlugProvider> provider;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;

    Steinberg::Vst::IAudioProcessor* proc = nullptr;

    Steinberg::IPtr<Steinberg::IPlugView> view;

    Handler handler;
    Frame frame;

    Steinberg::Vst::EventList events;
    Steinberg::Vst::ParameterChanges paramChanges;

    std::vector<float> inBuf[2];
    std::vector<float> outBuf[2];

    std::vector<float> effectScratch;

    std::array<
        Steinberg::Vst::AudioBusBuffers,
        16
    > inBuses{};

    std::array<
        Steinberg::Vst::AudioBusBuffers,
        16
    > outBuses{};

    double sampleRate = 44100.0;

    int maxBlock = 4096;

    int numInBuses = 0;
    int numOutBuses = 0;
    int numEventInBuses = 0;

    int inCh = 0;
    int outCh = 2;

    bool active = false;
    bool componentActive = false;
    bool processing = false;

    DWORD editorStyle =
        WS_OVERLAPPED |
        WS_CAPTION |
        WS_SYSMENU |
        WS_MINIMIZEBOX;
};
