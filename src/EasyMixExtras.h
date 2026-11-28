#pragma once
#define EASYMIX_EXTRAS_API 1
// EasyMix Studio: audio editing, loudness and MIDI generation utilities.
//  - LUFS meter (momentary / integrated / max) + stereo correlation meter
//  - Clip edit ops: reverse / mono / remove silence / pitch (resample) -> saved as new WAV (project-safe)
//  - MIDI generators: random melody (in scale), chord progression (+bass, 7th), drum step sequencer, fill-in
//  - Scale out-of-key check helper
//  - One-call ImGui UIs: DrawGeneratorUi / DrawLoudnessPanel / DrawDrumGrid
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Equalizer.h"   // Biquad
#include "PianoRoll.h"   // MidiNote, prdetail::*

namespace extras
{
    // =====================================================================
    // Loudness (ITU-R BS.1770 style, simplified gating on 100 ms blocks)
    // =====================================================================
    struct LufsMeter
    {
        Biquad shelfL, shelfR, hpL, hpR;
        unsigned rate = 0;
        unsigned blockLen = 4410;
        double accum = 0.0;
        unsigned count = 0;
        double blocks[4] = {};
        int bi = 0, filled = 0;
        std::vector<float> history = std::vector<float>(36000);
        size_t historyCount = 0, historyWrite = 0;
        std::atomic<float> momentary{ -70.0f };
        std::atomic<float> integrated{ -70.0f };
        std::atomic<float> maxMomentary{ -70.0f };

        static float ToLufs(double e) { return e > 1.0e-12 ? static_cast<float>(-0.691 + 10.0 * std::log10(e)) : -70.0f; }

        void reset()
        {
            shelfL.reset(); shelfR.reset(); hpL.reset(); hpR.reset();
            accum = 0.0; count = 0; bi = 0; filled = 0;
            for (double& b : blocks) b = 0.0;
            historyCount = historyWrite = 0;
            momentary.store(-70.0f); integrated.store(-70.0f); maxMomentary.store(-70.0f);
        }

        void setup(unsigned sr)
        {
            if (sr == rate) return;
            rate = sr;
            shelfL.setHighShelf(static_cast<float>(sr), 1681.97f, 3.99f, 0.707f);
            shelfR.copyCoeffsFrom(shelfL);
            hpL.setHighPass(static_cast<float>(sr), 38.14f, 0.5f);
            hpR.copyCoeffsFrom(hpL);
            blockLen = std::max(1u, sr / 10);
            reset();
        }

        // audio thread, interleaved stereo
        void process(const float* s, unsigned frames)
        {
            if (!rate) return;
            for (unsigned i = 0; i < frames; ++i)
            {
                const float l = hpL.process(shelfL.process(s[i * 2]));
                const float r = hpR.process(shelfR.process(s[i * 2 + 1]));
                accum += static_cast<double>(l) * l + static_cast<double>(r) * r;
                if (++count >= blockLen)
                {
                    push(accum / count);
                    accum = 0.0; count = 0;
                }
            }
        }

        void push(double e)
        {
            blocks[bi] = e; bi = (bi + 1) & 3;
            if (filled < 4) ++filled;
            history[historyWrite] = static_cast<float>(e);
            historyWrite = (historyWrite + 1) % history.size();
            historyCount = std::min(historyCount + 1, history.size());

            if (filled == 4)
            {
                const float l = ToLufs((blocks[0] + blocks[1] + blocks[2] + blocks[3]) * 0.25);
                momentary.store(l);
                if (l > maxMomentary.load()) maxMomentary.store(l);
            }
        }

        void computeIntegrated()
        {
            const double absoluteGate = std::pow(10.0, (-70.0 + 0.691) / 10.0);
            double sum = 0.0; size_t n = 0;
            for (size_t i = 0; i < historyCount; ++i)
                if (history[i] > absoluteGate) { sum += history[i]; ++n; }
            if (!n) { integrated.store(-70.0f); return; }
            const double gate = std::max(absoluteGate, sum / n * 0.1);
            double sum2 = 0.0; size_t n2 = 0;
            for (size_t i = 0; i < historyCount; ++i)
                if (history[i] > gate) { sum2 += history[i]; ++n2; }
            integrated.store(n2 ? ToLufs(sum2 / n2) : -70.0f);
        }
    };

    struct CorrelationMeter
    {
        double sll = 1.0e-9, srr = 1.0e-9, slr = 0.0;
        std::atomic<float> value{ 0.0f };

        void process(const float* s, unsigned frames)
        {
            const double k = 0.9998;
            for (unsigned i = 0; i < frames; ++i)
            {
                const double l = s[i * 2], r = s[i * 2 + 1];
                sll = sll * k + l * l;
                srr = srr * k + r * r;
                slr = slr * k + l * r;
            }
            value.store(static_cast<float>(std::clamp(slr / std::sqrt(sll * srr + 1.0e-18), -1.0, 1.0)));
        }
    };

    inline void DrawLoudnessPanel(LufsMeter& lm, const CorrelationMeter& cm)
    {
        const float mom = lm.momentary.load(), integ = lm.integrated.load(), mx = lm.maxMomentary.load();
        ImGui::Text("Momentary  %6.1f LUFS", mom);
        ImGui::ProgressBar(std::clamp((mom + 60.0f) / 60.0f, 0.0f, 1.0f), ImVec2(-1, 8), "");
        ImGui::Text("Integrated %6.1f LUFS", integ);
        ImGui::Text("Max (M)    %6.1f LUFS", mx);
        if (ImGui::Button("Reset LUFS")) lm.reset();
        const float c = cm.value.load();
        ImGui::Text("Correlation %+.2f %s", c, c < 0.0f ? "(phase problem!)" : "");
        ImGui::ProgressBar((c + 1.0f) * 0.5f, ImVec2(-1, 8), "");
    }

    // =====================================================================
    // Audio clip editing (result is written to a new WAV so saving the project keeps it)
    // =====================================================================
    inline bool SaveWavF32(const std::string& path, const std::vector<float>& pcm, unsigned sr)
    {
        std::ofstream o(path, std::ios::binary);
        if (!o) return false;
        auto w32 = [&](uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
        auto w16 = [&](uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); };
        const uint32_t bytes = static_cast<uint32_t>(pcm.size() * sizeof(float));
        o.write("RIFF", 4); w32(36 + bytes); o.write("WAVE", 4);
        o.write("fmt ", 4); w32(16); w16(3); w16(2); w32(sr); w32(sr * 8); w16(8); w16(32);
        o.write("data", 4); w32(bytes);
        o.write(reinterpret_cast<const char*>(pcm.data()), bytes);
        return static_cast<bool>(o);
    }

    // Clip needs: pcm (shared_ptr<vector<float>>), path, sourceOffsetSeconds, duration, fadeInSeconds, fadeOutSeconds
    // After success call buildWaveform(clip).
    template <class Clip, class Fn>
    bool EditClipPcm(Clip& c, unsigned sr, const char* suffix, Fn fn)
    {
        if (!c.pcm) return false;
        const long long total = static_cast<long long>(c.pcm->size() / 2);
        const long long off = std::llround(std::max(0.0f, c.sourceOffsetSeconds) * sr);
        long long len = std::llround(std::max(0.0f, c.duration) * sr);
        if (off >= total || len <= 0) return false;
        len = std::min(len, total - off);

        std::vector<float> buf(c.pcm->begin() + off * 2, c.pcm->begin() + (off + len) * 2);
        fn(buf);
        if (buf.size() < 4) return false;

        const std::string np = c.path + suffix + ".wav";
        if (!SaveWavF32(np, buf, sr)) return false;

        c.pcm = std::make_shared<std::vector<float>>(std::move(buf));
        c.path = np;
        c.sourceOffsetSeconds = 0.0f;
        c.duration = static_cast<float>(c.pcm->size() / 2) / static_cast<float>(sr);
        c.fadeInSeconds = std::min(c.fadeInSeconds, c.duration);
        c.fadeOutSeconds = std::min(c.fadeOutSeconds, c.duration);
        return true;
    }

    inline void ReversePcm(std::vector<float>& b)
    {
        const size_t n = b.size() / 2;
        for (size_t i = 0; i < n / 2; ++i)
        {
            std::swap(b[i * 2], b[(n - 1 - i) * 2]);
            std::swap(b[i * 2 + 1], b[(n - 1 - i) * 2 + 1]);
        }
    }

    inline void ToMonoPcm(std::vector<float>& b)
    {
        for (size_t i = 0; i + 1 < b.size(); i += 2)
            b[i] = b[i + 1] = (b[i] + b[i + 1]) * 0.5f;
    }

    inline void RemoveSilencePcm(std::vector<float>& b, unsigned sr, float thresholdDb = -50.0f, float minMs = 200.0f)
    {
        const float thr = std::pow(10.0f, thresholdDb / 20.0f);
        const size_t n = b.size() / 2;
        const size_t minRun = static_cast<size_t>(minMs * 0.001f * sr);
        std::vector<float> out;
        out.reserve(b.size());
        size_t i = 0;
        while (i < n)
        {
            if (std::max(std::fabs(b[i * 2]), std::fabs(b[i * 2 + 1])) < thr)
            {
                size_t j = i;
                while (j < n && std::max(std::fabs(b[j * 2]), std::fabs(b[j * 2 + 1])) < thr) ++j;
                if (j - i < minRun)
                    out.insert(out.end(), b.begin() + i * 2, b.begin() + j * 2);
                i = j;
            }
            else
            {
                out.push_back(b[i * 2]); out.push_back(b[i * 2 + 1]);
                ++i;
            }
        }
        b.swap(out);
    }

    // pitch change by resampling (duration changes too, like tape)
    inline void PitchResamplePcm(std::vector<float>& b, float semitones)
    {
        const double ratio = std::pow(2.0, semitones / 12.0);
        const size_t n = b.size() / 2;
        if (n < 2) return;
        const size_t m = static_cast<size_t>(n / ratio);
        std::vector<float> out(m * 2);
        for (size_t i = 0; i < m; ++i)
        {
            const double p = i * ratio;
            const size_t i0 = std::min(n - 1, static_cast<size_t>(p));
            const size_t i1 = std::min(n - 1, i0 + 1);
            const float f = static_cast<float>(p - i0);
            out[i * 2] = b[i0 * 2] * (1.0f - f) + b[i1 * 2] * f;
            out[i * 2 + 1] = b[i0 * 2 + 1] * (1.0f - f) + b[i1 * 2 + 1] * f;
        }
        b.swap(out);
    }

    // =====================================================================
    // MIDI generators
    // =====================================================================
    inline bool OutOfScale(const MidiNote& n, int root, int type)
    {
        return type != 0 && !prdetail::InScale(n.pitch, root, type);
    }

    inline int ScaleMove(int p, int steps, int root, int type)
    {
        if (steps >= 0) return prdetail::ScaleStep(p, steps, root, type);
        for (int k = 0; k < -steps; ++k)
        {
            int q = p - 1;
            while (q > 0 && !prdetail::InScale(q, root, type == 0 ? 1 : type)) --q;
            p = std::max(0, q);
        }
        return p;
    }

    inline void RandomMelody(std::vector<MidiNote>& notes, int track, int& nextId, float startBeat, int bars,
        int root, int type, int lo, int hi, float step, float density, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        const int ty = type == 0 ? 1 : type;
        int cur = prdetail::NearestInScale(std::clamp((lo + hi) / 2, 0, 127), root, ty);
        for (float b = 0.0f; b < bars * 4.0f - 0.001f; b += step)
        {
            if (u(rng) > density) continue;
            const int delta = static_cast<int>(u(rng) * 5.0f) - 2;   // -2..2 scale steps
            cur = ScaleMove(cur, delta, root, ty);
            if (cur < lo) cur = prdetail::NearestInScale(lo, root, ty);
            if (cur > hi) cur = ScaleMove(cur, -3, root, ty);
            cur = std::clamp(cur, 0, 127);
            MidiNote n;
            n.id = nextId++; n.trackIndex = track; n.pitch = cur;
            n.startBeat = startBeat + b;
            n.lengthBeats = step * (u(rng) < 0.25f ? 2.0f : 1.0f);
            n.velocity = 0.6f + 0.35f * u(rng);
            notes.push_back(n);
        }
    }

    inline const char* const* ProgressionNames(int& n)
    {
        static const char* names[] = { "I - V - vi - IV", "vi - IV - I - V", "I - vi - IV - V", "ii - V - I - I",
            "i - VI - III - VII", "I - IV - V - IV" };
        n = 6;
        return names;
    }

    inline void ChordProgression(std::vector<MidiNote>& notes, int track, int& nextId, float startBeat, int bars,
        int root, int type, int progression, bool seventh, bool bass)
    {
        static const int degs[6][4] = { {0,4,5,3}, {5,3,0,4}, {0,5,3,4}, {1,4,0,0}, {0,5,2,6}, {0,3,4,3} };
        const int ty = type == 0 ? 1 : type;
        const int base = prdetail::NearestInScale(60 + root, root, ty);
        for (int bar = 0; bar < bars; ++bar)
        {
            const int deg = degs[std::clamp(progression, 0, 5)][bar % 4];
            const int r = prdetail::ScaleStep(base, deg, root, ty);
            std::vector<int> ps = { r, prdetail::ScaleStep(r, 2, root, ty), prdetail::ScaleStep(r, 4, root, ty) };
            if (seventh) ps.push_back(prdetail::ScaleStep(r, 6, root, ty));
            if (bass) ps.push_back(std::max(0, r - 24));
            for (int p : ps)
            {
                MidiNote n;
                n.id = nextId++; n.trackIndex = track; n.pitch = std::clamp(p, 0, 127);
                n.startBeat = startBeat + bar * 4.0f; n.lengthBeats = 4.0f; n.velocity = 0.7f;
                notes.push_back(n);
            }
        }
    }

    // ---- drum step sequencer (8 rows x 16 steps) ----
    struct DrumPattern
    {
        static constexpr int Rows = 8, Steps = 16;
        bool step[Rows][Steps] = {};
        int pitch[Rows] = { 36, 38, 42, 46, 39, 49, 45, 37 };
        float vel = 0.85f;
        float swing = 0.0f;

        static const char* RowName(int r)
        {
            static const char* n[] = { "Kick", "Snare", "HH C", "HH O", "Clap", "Crash", "Tom", "Rim" };
            return n[r];
        }

        void clear() { for (auto& r : step) for (bool& s : r) s = false; }

        void preset(int k)
        {
            clear();
            if (k == 0) // rock
            {
                for (int s : { 0, 8 }) step[0][s] = true;
                for (int s : { 4, 12 }) step[1][s] = true;
                for (int s = 0; s < 16; s += 2) step[2][s] = true;
            }
            else if (k == 1) // four on the floor
            {
                for (int s = 0; s < 16; s += 4) step[0][s] = true;
                for (int s : { 4, 12 }) step[4][s] = true;
                for (int s = 2; s < 16; s += 4) step[3][s] = true;
                for (int s = 0; s < 16; s += 2) step[2][s] = true;
            }
            else if (k == 2) // trap
            {
                for (int s : { 0, 7, 10 }) step[0][s] = true;
                step[1][8] = true;
                for (int s = 0; s < 16; ++s) step[2][s] = true;
            }
        }

        void randomize(unsigned seed, float density)
        {
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            clear();
            for (int s = 0; s < 16; s += 4) step[0][s] = true;
            step[1][4] = step[1][12] = true;
            for (int s = 0; s < 16; ++s)
            {
                if (u(rng) < density * 0.25f) step[0][s] = true;
                if (u(rng) < density * 0.10f) step[1][s] = true;
                if (u(rng) < density * 0.70f) step[2][s] = true;
                if (u(rng) < density * 0.08f) step[3][s] = true;
                if (u(rng) < density * 0.05f) step[7][s] = true;
            }
        }

        void toNotes(std::vector<MidiNote>& notes, int track, int& nextId, float startBeat, int bars) const
        {
            for (int b = 0; b < bars; ++b)
                for (int r = 0; r < Rows; ++r)
                    for (int s = 0; s < Steps; ++s)
                    {
                        if (!step[r][s]) continue;
                        MidiNote n;
                        n.id = nextId++; n.trackIndex = track; n.pitch = pitch[r];
                        n.startBeat = startBeat + b * 4.0f + s * 0.25f + ((s & 1) ? swing * 0.25f : 0.0f);
                        n.lengthBeats = 0.25f;
                        n.velocity = std::clamp(vel * (s % 4 == 0 ? 1.0f : 0.8f), 0.05f, 1.0f);
                        notes.push_back(n);
                    }
        }
    };

    inline void AddFillIn(std::vector<MidiNote>& notes, int track, int& nextId, float barStartBeat, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        static const int toms[] = { 50, 48, 47, 45, 43, 41 };
        for (int s = 0; s < 4; ++s)
        {
            MidiNote n;
            n.id = nextId++; n.trackIndex = track;
            n.pitch = u(rng) < 0.4f ? 38 : toms[static_cast<int>(u(rng) * 5.99f)];
            n.startBeat = barStartBeat + 3.0f + s * 0.25f;
            n.lengthBeats = 0.25f;
            n.velocity = 0.6f + 0.1f * s;
            notes.push_back(n);
        }
    }

    inline void DrawDrumGrid(DrumPattern& d)
    {
        for (int r = 0; r < DrumPattern::Rows; ++r)
        {
            ImGui::PushID(r);
            ImGui::TextUnformatted(DrumPattern::RowName(r));
            for (int s = 0; s < DrumPattern::Steps; ++s)
            {
                ImGui::SameLine(r >= 0 ? 60.0f + s * 24.0f + (s / 4) * 6.0f : 0.0f);
                ImGui::PushID(s);
                ImGui::Checkbox("##s", &d.step[r][s]);
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    }

    // ---- one-call generator UI ----
    struct ExtrasState
    {
        DrumPattern drum;
        int bars = 4;
        float startBeat = 0.0f;
        int prog = 0;
        bool seventh = false, bass = true;
        int melLo = 60, melHi = 84;
        float melStep = 0.5f, melDensity = 0.65f;
        unsigned seed = 1;
        float drumDensity = 0.6f;
        float pitchSemis = 0.0f;
    };

    // call between ImGui::Begin / End. Returns true if notes were added.
    inline bool DrawGeneratorUi(ExtrasState& ex, std::vector<MidiNote>& notes, int track, int& nextId,
        int scaleRoot, int scaleType, float playheadBeat)
    {
        bool added = false;
        if (track < 0) { ImGui::TextDisabled("Select a track."); return false; }

        ImGui::SetNextItemWidth(80.0f); ImGui::DragInt("Bars", &ex.bars, 0.2f, 1, 64);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f); ImGui::DragFloat("Start beat", &ex.startBeat, 0.25f, 0.0f, 4096.0f);
        ImGui::SameLine();
        if (ImGui::Button("Start = playhead")) ex.startBeat = std::max(0.0f, std::round(playheadBeat / 4.0f) * 4.0f);
        ImGui::TextDisabled("Key / Scale: use the Piano Roll settings.");

        ImGui::SeparatorText("Chord progression");
        int pn = 0; const char* const* names = ProgressionNames(pn);
        ImGui::SetNextItemWidth(160.0f); ImGui::Combo("##prog", &ex.prog, names, pn);
        ImGui::SameLine(); ImGui::Checkbox("7th", &ex.seventh);
        ImGui::SameLine(); ImGui::Checkbox("Bass", &ex.bass);
        ImGui::SameLine();
        if (ImGui::Button("Generate chords"))
        {
            ChordProgression(notes, track, nextId, ex.startBeat, ex.bars, scaleRoot, scaleType, ex.prog, ex.seventh, ex.bass); added = true;
        }

        ImGui::SeparatorText("Random melody");
        ImGui::SetNextItemWidth(80.0f); ImGui::DragInt("Low", &ex.melLo, 0.5f, 0, 127);
        ImGui::SameLine(); ImGui::SetNextItemWidth(80.0f); ImGui::DragInt("High", &ex.melHi, 0.5f, 0, 127);
        ImGui::SetNextItemWidth(80.0f); ImGui::SliderFloat("Density", &ex.melDensity, 0.1f, 1.0f);
        ImGui::SameLine(); ImGui::SetNextItemWidth(80.0f); ImGui::SliderFloat("Step", &ex.melStep, 0.125f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::Button("Generate melody"))
        {
            RandomMelody(notes, track, nextId, ex.startBeat, ex.bars, scaleRoot, scaleType, ex.melLo, ex.melHi, ex.melStep, ex.melDensity, ex.seed++); added = true;
        }

        ImGui::SeparatorText("Drum step sequencer");
        DrawDrumGrid(ex.drum);
        if (ImGui::Button("Rock")) ex.drum.preset(0);
        ImGui::SameLine(); if (ImGui::Button("4-on-floor")) ex.drum.preset(1);
        ImGui::SameLine(); if (ImGui::Button("Trap")) ex.drum.preset(2);
        ImGui::SameLine(); if (ImGui::Button("Clear")) ex.drum.clear();
        ImGui::SameLine(); if (ImGui::Button("Random")) ex.drum.randomize(ex.seed++, ex.drumDensity);
        ImGui::SetNextItemWidth(90.0f); ImGui::SliderFloat("Swing", &ex.drum.swing, 0.0f, 0.5f);
        ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::SliderFloat("Vel", &ex.drum.vel, 0.1f, 1.0f);
        ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::SliderFloat("Density", &ex.drumDensity, 0.1f, 1.0f);
        if (ImGui::Button("Write pattern to track"))
        {
            ex.drum.toNotes(notes, track, nextId, ex.startBeat, ex.bars); added = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add fill-in (last bar)"))
        {
            AddFillIn(notes, track, nextId, ex.startBeat + (ex.bars - 1) * 4.0f, ex.seed++); added = true;
        }
        ImGui::TextDisabled("Tip: pick a drum kit instrument on the track first.");
        return added;
    }

    struct MidiToolsState
    {
        float grid = 0.25f;
        float swing = 0.0f;
        float humanizeBeats = 0.03f;
        float humanizeVelocity = 0.08f;
        int transpose = 0;
        int selectedScaleRoot = 0;
        int selectedScaleType = 0;
        unsigned seed = 1;
    };

    inline int QuantizeTrack(std::vector<MidiNote>& notes, int track, float grid, float strength = 1.0f)
    {
        if (grid <= 0.0f) return 0;
        int changed = 0;
        for (auto& n : notes)
        {
            if (n.trackIndex != track) continue;
            const float q = std::round(n.startBeat / grid) * grid;
            const float v = n.startBeat + (q - n.startBeat) * std::clamp(strength, 0.0f, 1.0f);
            if (std::fabs(v - n.startBeat) > 0.0001f) { n.startBeat = std::max(0.0f, v); ++changed; }
        }
        return changed;
    }

    inline int SwingTrack(std::vector<MidiNote>& notes, int track, float amount, float grid = 0.5f)
    {
        if (grid <= 0.0f) return 0;
        amount = std::clamp(amount, 0.0f, 0.5f);
        int changed = 0;
        for (auto& n : notes)
        {
            const int cell = static_cast<int>(std::floor(n.startBeat / grid));
            if (n.trackIndex == track && (cell & 1) != 0) { n.startBeat += grid * amount; ++changed; }
        }
        return changed;
    }

    inline int HumanizeTrack(std::vector<MidiNote>& notes, int track, float beatAmount, float velocityAmount, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> beat(-std::max(0.0f, beatAmount), std::max(0.0f, beatAmount));
        std::uniform_real_distribution<float> vel(-std::max(0.0f, velocityAmount), std::max(0.0f, velocityAmount));
        int changed = 0;
        for (auto& n : notes) if (n.trackIndex == track)
        {
            n.startBeat = std::max(0.0f, n.startBeat + beat(rng));
            n.velocity = std::clamp(n.velocity + vel(rng), 0.01f, 1.0f);
            ++changed;
        }
        return changed;
    }

    inline int TransposeTrack(std::vector<MidiNote>& notes, int track, int semitones)
    {
        int changed = 0;
        for (auto& n : notes) if (n.trackIndex == track)
        {
            const int p = std::clamp(n.pitch + semitones, 0, 127);
            if (p != n.pitch) { n.pitch = p; ++changed; }
        }
        return changed;
    }

    inline int MoveTrackToScale(std::vector<MidiNote>& notes, int track, int root, int scale)
    {
        int changed = 0;
        for (auto& n : notes) if (n.trackIndex == track && OutOfScale(n, root, scale))
        {
            n.pitch = prdetail::NearestInScale(n.pitch, root, scale);
            ++changed;
        }
        return changed;
    }

    inline void NormalizePcm(std::vector<float>& pcm, float targetPeak = 0.95f)
    {
        float peak = 0.0f;
        for (float v : pcm) peak = std::max(peak, std::fabs(v));
        if (peak <= 1.0e-8f) return;
        const float gain = std::clamp(targetPeak, 0.01f, 1.0f) / peak;
        for (float& v : pcm) v = std::clamp(v * gain, -1.0f, 1.0f);
    }

    inline void ApplyFadePcm(std::vector<float>& pcm, unsigned sr, float fadeInSeconds, float fadeOutSeconds)
    {
        const size_t frames = pcm.size() / 2;
        if (!frames || !sr) return;
        const size_t fi = std::min(frames, static_cast<size_t>(std::max(0.0f, fadeInSeconds) * sr));
        const size_t fo = std::min(frames, static_cast<size_t>(std::max(0.0f, fadeOutSeconds) * sr));
        for (size_t i = 0; i < frames; ++i)
        {
            float g = 1.0f;
            if (fi && i < fi) g = std::min(g, static_cast<float>(i) / static_cast<float>(fi));
            if (fo && i >= frames - fo) g = std::min(g, static_cast<float>(frames - 1 - i) / static_cast<float>(fo));
            pcm[i * 2] *= g; pcm[i * 2 + 1] *= g;
        }
    }

    inline bool DrawMidiToolsUi(MidiToolsState& st, std::vector<MidiNote>& notes, int track)
    {
        bool changed = false;
        if (track < 0) { ImGui::TextDisabled("Select a track first."); return false; }
        ImGui::Text("Selected track: %d", track + 1);
        ImGui::SetNextItemWidth(110.0f); ImGui::DragFloat("Quantize grid", &st.grid, 0.01f, 0.0625f, 4.0f, "%.4f beat");
        if (ImGui::Button("Quantize")) changed |= QuantizeTrack(notes, track, st.grid) > 0;
        ImGui::SameLine();
        if (ImGui::Button("Swing")) changed |= SwingTrack(notes, track, st.swing, st.grid) > 0;
        ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::SliderFloat("##swing", &st.swing, 0.0f, 0.5f, "Swing %.2f");
        ImGui::Separator();
        ImGui::SetNextItemWidth(90.0f); ImGui::SliderInt("Transpose", &st.transpose, -48, 48);
        ImGui::SameLine(); if (ImGui::Button("Apply transpose")) changed |= TransposeTrack(notes, track, st.transpose) > 0;
        ImGui::Separator();
        ImGui::SliderFloat("Timing humanize", &st.humanizeBeats, 0.0f, 0.2f, "%.3f beat");
        ImGui::SliderFloat("Velocity humanize", &st.humanizeVelocity, 0.0f, 0.3f, "%.2f");
        if (ImGui::Button("Humanize")) changed |= HumanizeTrack(notes, track, st.humanizeBeats, st.humanizeVelocity, st.seed++) > 0;
        ImGui::Separator();
        const char* roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        ImGui::SetNextItemWidth(90.0f); ImGui::Combo("Key", &st.selectedScaleRoot, roots, 12);
        static const char* scales[] = { "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Major Pentatonic", "Minor Pentatonic", "Blues", "Dorian", "Mixolydian", "Phrygian", "Lydian" };
        ImGui::SetNextItemWidth(150.0f); ImGui::Combo("Scale", &st.selectedScaleType, scales, 12);
        if (ImGui::Button("Correct out-of-scale notes")) changed |= MoveTrackToScale(notes, track, st.selectedScaleRoot, st.selectedScaleType) > 0;
        return changed;
    }

}
