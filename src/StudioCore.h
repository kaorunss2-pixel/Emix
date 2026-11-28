#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace studio
{
    // Single producer (capture callback), single consumer (disk writer).
    class RecordRing
    {
    public:
        explicit RecordRing(size_t capacity) : samples_(std::max<size_t>(1, capacity)) {}

        size_t write(const float* input, size_t count)
        {
            const size_t w = write_.load(std::memory_order_relaxed);
            const size_t r = read_.load(std::memory_order_acquire);
            const size_t n = std::min(count, samples_.size() - (w - r));
            for (size_t i = 0; i < n; ++i) samples_[(w + i) % samples_.size()] = input[i];
            write_.store(w + n, std::memory_order_release);
            return n;
        }

        size_t read(float* output, size_t count)
        {
            const size_t r = read_.load(std::memory_order_relaxed);
            const size_t w = write_.load(std::memory_order_acquire);
            const size_t n = std::min(count, w - r);
            for (size_t i = 0; i < n; ++i) output[i] = samples_[(r + i) % samples_.size()];
            read_.store(r + n, std::memory_order_release);
            return n;
        }

        size_t size() const
        {
            return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
        }

    private:
        std::vector<float> samples_;
        std::atomic<size_t> read_{ 0 }, write_{ 0 };
    };

    struct ActiveMidiNote
    {
        int id;
        int pitch;
        long long endFrame;
    };

    struct MidiEvent
    {
        int offset;
        bool on;
        int pitch;
        float velocity;
        int id;
    };

    // Chases sustained notes after a seek/loop, without retriggering on normal blocks.
    template <class Note>
    void ScheduleMidiBlock(const std::vector<Note>& notes, int track, long long start, int frames,
        double framesPerBeat, std::vector<ActiveMidiNote>& active, std::vector<MidiEvent>& events)
    {
        events.clear();
        const long long end = start + frames;
        for (const Note& note : notes)
        {
            if (note.trackIndex != track || note.lengthBeats <= 0.0f) continue;
            const long long onset = static_cast<long long>(note.startBeat * framesPerBeat);
            const long long release = std::max(onset + 1,
                static_cast<long long>((note.startBeat + note.lengthBeats) * framesPerBeat));
            const bool startsHere = onset >= start && onset < end;
            const bool needsChase = onset < start && release > start &&
                std::none_of(active.begin(), active.end(), [&](const ActiveMidiNote& a) { return a.id == note.id; });
            if (!startsHere && !needsChase) continue;
            events.push_back({ startsHere ? static_cast<int>(onset - start) : 0,
                true, note.pitch, note.velocity, note.id });
            active.push_back({ note.id, note.pitch, release });
        }

        for (size_t i = 0; i < active.size();)
        {
            if (active[i].endFrame < end)
            {
                events.push_back({ static_cast<int>(std::clamp(active[i].endFrame - start,
                    0LL, static_cast<long long>(frames))), false, active[i].pitch, 0.0f, active[i].id });
                active.erase(active.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else ++i;
        }
        std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b)
            { return a.offset == b.offset ? (!a.on && b.on) : a.offset < b.offset; });
    }

    inline float NormalizationGainDb(float integratedLufs, float peak, float targetLufs, float ceilingDb)
    {
        if (!std::isfinite(integratedLufs) || !std::isfinite(peak) ||
            integratedLufs <= -69.0f || peak <= 1.0e-6f) return 0.0f;
        return std::min(targetLufs - integratedLufs,
            ceilingDb - 20.0f * std::log10(peak));
    }
}
