#pragma once
// EasyMix Studio: OtoMad audio slicing, pitch detection and timing tools.
//  - Detect the root pitch of a sample (YIN).
//  - Build audio clips from MIDI notes, with tape or WSOLA pitch modes.
//  - Natural, trim-to-note and fit-to-note duration modes.
//  - Velocity/RMS matching, fades, reversal and note muting.
//  - Clip pitch and time-stretch processing.
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "PianoRoll.h"
#include "EasyMixExtras.h"

namespace otomad
{
    using Pcm = std::vector<float>;   // interleaved stereo

    struct State
    {
        std::shared_ptr<Pcm> srcPcm;
        std::string srcPath, srcName;
        float srcOffset = 0.0f, srcDur = 0.0f;
        float detectedMidi = -1.0f, detectConf = 0.0f;

        int rootMode = 0;        // 0 = auto(detected), 1 = manual
        int rootNote = 60;
        float tuneCents = 0.0f;
        int pitchMode = 0;       // 0 = tape, 1 = keep length
        int lenMode = 1;         // 0 natural, 1 trim, 2 fit
        bool mono = true;
        float velAmount = 0.5f;
        bool autoVol = true;
        float targetDb = -14.0f;
        float fadeMs = 8.0f;
        int reverseEvery = 0;
        bool muteNotes = true;

        float clipSemis = 0.0f;
        float stretchBeats = 1.0f;
    };

    // ------------------------------------------------------------ level
    inline void MeasureRaw(const float* p, size_t frames, float& rms, float& peak)
    {
        double sum = 0.0;
        size_t cnt = 0;
        peak = 0.0f;
        for (size_t i = 0; i < frames; ++i)
        {
            const float l = p[i * 2], r = p[i * 2 + 1];
            const float a = std::max(std::fabs(l), std::fabs(r));
            peak = std::max(peak, a);
            if (a > 0.00316f) { sum += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r); ++cnt; }
        }
        rms = cnt ? static_cast<float>(std::sqrt(sum / static_cast<double>(cnt))) : 0.0f;
    }

    inline float GainFor(float rms, float peak, float targetDb)
    {
        if (rms < 1.0e-5f || peak < 1.0e-6f) return 1.0f;
        float g = std::pow(10.0f, targetDb / 20.0f) / rms;
        g = std::min(g, 0.98f / peak);
        return std::clamp(g, 0.05f, 4.0f);
    }

    // ------------------------------------------------------------ DSP
    inline Pcm Resample(const Pcm& b, double ratio)   // ratio > 1 : higher pitch, shorter
    {
        const long long n = static_cast<long long>(b.size() / 2);
        if (n < 4 || ratio <= 0.0 || std::fabs(ratio - 1.0) < 1.0e-6) return b;
        const long long m = std::max(1LL, static_cast<long long>(static_cast<double>(n) / ratio));
        Pcm out(static_cast<size_t>(m) * 2);
        auto at = [&](long long i, int c) { i = std::clamp(i, 0LL, n - 1); return b[static_cast<size_t>(i) * 2 + c]; };
        for (long long i = 0; i < m; ++i)
        {
            const double p = static_cast<double>(i) * ratio;
            const long long i1 = static_cast<long long>(p);
            const float f = static_cast<float>(p - static_cast<double>(i1));
            for (int c = 0; c < 2; ++c)
            {
                const float y0 = at(i1 - 1, c), y1 = at(i1, c), y2 = at(i1 + 1, c), y3 = at(i1 + 2, c);
                const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
                const float bb = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float cc = -0.5f * y0 + 0.5f * y2;
                out[static_cast<size_t>(i) * 2 + c] = ((a * f + bb) * f + cc) * f + y1;
            }
        }
        return out;
    }

    // time stretch without pitch change (WSOLA). output length = n * stretch
    inline Pcm Wsola(const Pcm& in, double stretch)
    {
        const long long n = static_cast<long long>(in.size() / 2);
        const int N = 2048, H = 1024, D = 384;
        stretch = std::clamp(stretch, 0.05, 16.0);
        if (n < N * 2) return Resample(in, 1.0 / stretch);

        const long long outLen = std::max(1LL, static_cast<long long>(std::llround(static_cast<double>(n) * stretch)));
        Pcm out(static_cast<size_t>(outLen + N) * 2, 0.0f);
        std::vector<float> wsum(static_cast<size_t>(outLen + N), 0.0f);
        std::vector<float> w(N);
        for (int k = 0; k < N; ++k) w[k] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(k) / static_cast<float>(N));
        std::vector<float> mono(static_cast<size_t>(n));
        for (long long i = 0; i < n; ++i) mono[static_cast<size_t>(i)] = 0.5f * (in[static_cast<size_t>(i) * 2] + in[static_cast<size_t>(i) * 2 + 1]);

        long long prev = 0;
        for (long long o = 0, f = 0; o < outLen; o += H, ++f)
        {
            long long a = 0;
            if (f > 0)
            {
                const long long nom = static_cast<long long>(std::llround(static_cast<double>(o) / stretch));
                a = std::clamp(nom, 0LL, n - N);
                const long long lo = std::max(0LL, nom - D), hi = std::min(n - N, nom + D);
                const long long ref = prev + H;
                if (hi >= lo && ref + H <= n)
                {
                    double best = -1.0e30;
                    for (long long c = lo; c <= hi; c += 2)
                    {
                        double cr = 0.0, en = 1.0e-9;
                        for (int j = 0; j < H; j += 4)
                        {
                            const float x = mono[static_cast<size_t>(c + j)];
                            cr += static_cast<double>(x) * mono[static_cast<size_t>(ref + j)];
                            en += static_cast<double>(x) * x;
                        }
                        const double sc = cr / std::sqrt(en);
                        if (sc > best) { best = sc; a = c; }
                    }
                }
            }
            for (int k = 0; k < N; ++k)
            {
                const size_t oi = static_cast<size_t>(o + k), ii = static_cast<size_t>(a + k);
                out[oi * 2] += in[ii * 2] * w[k];
                out[oi * 2 + 1] += in[ii * 2 + 1] * w[k];
                wsum[oi] += w[k];
            }
            prev = a;
        }
        for (long long i = 0; i < outLen; ++i)
        {
            const float ws = wsum[static_cast<size_t>(i)];
            if (ws > 1.0e-3f) { out[static_cast<size_t>(i) * 2] /= ws; out[static_cast<size_t>(i) * 2 + 1] /= ws; }
        }
        out.resize(static_cast<size_t>(outLen) * 2);
        return out;
    }

    inline Pcm Pitched(const Pcm& src, double semis, int mode)
    {
        if (std::fabs(semis) < 0.001) return src;
        const double r = std::pow(2.0, semis / 12.0);
        if (mode == 1) return Resample(Wsola(src, r), r);   // stretch by r, then resample back to original length
        return Resample(src, r);
    }

    inline Pcm FitTo(const Pcm& b, long long targetFrames)
    {
        const long long n = static_cast<long long>(b.size() / 2);
        if (n < 2 || targetFrames < 16) return b;
        const double s = std::clamp(static_cast<double>(targetFrames) / static_cast<double>(n), 0.05, 16.0);
        if (std::fabs(s - 1.0) < 0.005) return b;
        return Wsola(b, s);
    }

    // YIN pitch detection. returns fractional MIDI note, or -1
    inline float DetectMidi(const Pcm& b, unsigned sr, float* conf = nullptr)
    {
        if (conf) *conf = 0.0f;
        const long long n = static_cast<long long>(b.size() / 2);
        const int W = 2048;
        const int maxTau = static_cast<int>(sr / 55u);
        const int minTau = std::max(2, static_cast<int>(sr / 1100u));
        if (n < W + maxTau + 8) return -1.0f;

        std::vector<float> m(static_cast<size_t>(n));
        for (long long i = 0; i < n; ++i) m[static_cast<size_t>(i)] = 0.5f * (b[static_cast<size_t>(i) * 2] + b[static_cast<size_t>(i) * 2 + 1]);

        long long bestS = 0;
        double bestE = -1.0;
        for (long long s = 0; s + W + maxTau < n; s += W / 2)
        {
            double e = 0.0;
            for (int j = 0; j < W; j += 2) e += static_cast<double>(m[static_cast<size_t>(s + j)]) * m[static_cast<size_t>(s + j)];
            if (e > bestE) { bestE = e; bestS = s; }
        }
        if (bestE < 1.0e-6) return -1.0f;

        std::vector<float> d(static_cast<size_t>(maxTau) + 1, 0.0f), cm(static_cast<size_t>(maxTau) + 1, 1.0f);
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < W; j += 2)
            {
                const float x = m[static_cast<size_t>(bestS + j)] - m[static_cast<size_t>(bestS + j + tau)];
                sum += static_cast<double>(x) * x;
            }
            d[tau] = static_cast<float>(sum);
        }
        double run = 0.0;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            run += d[tau];
            cm[tau] = run > 0.0 ? static_cast<float>(d[tau] * tau / run) : 1.0f;
        }

        int tau = -1;
        for (int t = minTau; t < maxTau; ++t)
        {
            if (cm[t] < 0.15f)
            {
                while (t + 1 < maxTau && cm[t + 1] < cm[t]) ++t;
                tau = t;
                break;
            }
        }
        if (tau < 0)
        {
            float best = 1.0e9f;
            for (int t = minTau; t < maxTau; ++t) if (cm[t] < best) { best = cm[t]; tau = t; }
        }
        if (tau < 1) return -1.0f;
        if (conf) *conf = std::clamp(1.0f - cm[tau], 0.0f, 1.0f);

        float tauf = static_cast<float>(tau);
        if (tau > 1 && tau < maxTau)
        {
            const float p0 = cm[tau - 1], p1 = cm[tau], p2 = cm[tau + 1];
            const float den = p0 - 2.0f * p1 + p2;
            if (std::fabs(den) > 1.0e-9f) tauf += std::clamp(0.5f * (p0 - p2) / den, -1.0f, 1.0f);
        }
        const float freq = static_cast<float>(sr) / tauf;
        return 69.0f + 12.0f * std::log2(freq / 440.0f);
    }

    inline Pcm Region(const State& st, unsigned sr)
    {
        Pcm r;
        if (!st.srcPcm) return r;
        const long long total = static_cast<long long>(st.srcPcm->size() / 2);
        const long long off = std::llround(std::max(0.0f, st.srcOffset) * sr);
        long long len = std::llround(std::max(0.0f, st.srcDur) * sr);
        if (off >= total || len <= 0) return r;
        len = std::min(len, total - off);
        r.assign(st.srcPcm->begin() + off * 2, st.srcPcm->begin() + (off + len) * 2);
        return r;
    }

    // ------------------------------------------------------------ bake
    struct Baked
    {
        std::shared_ptr<Pcm> pcm;
        std::string path, name;
        float start = 0.0f, dur = 0.0f, gain = 1.0f, fadeIn = 0.0f, fadeOut = 0.0f;
    };
    struct BakeResult
    {
        std::vector<Baked> clips;
        std::string error;
        int unique = 0;
    };

    // notes must already be filtered to one track and sorted by startBeat
    inline BakeResult Bake(const State& st, const std::vector<MidiNote>& notes, float bpm, unsigned sr)
    {
        namespace fs = std::filesystem;
        BakeResult res;
        const Pcm region = Region(st, sr);
        if (region.size() < 8) { res.error = "No source sample. Select an audio clip and press 'Use selected clip as source'."; return res; }
        if (notes.empty()) { res.error = "No MIDI notes on the selected track."; return res; }
        if (notes.size() > 3000) { res.error = "Too many notes (max 3000)."; return res; }
        if (bpm <= 0.0f) { res.error = "Invalid BPM."; return res; }

        float root = static_cast<float>(st.rootNote);
        if (st.rootMode == 0 && st.detectedMidi >= 0.0f) root = st.detectedMidi;

        std::error_code ec;
        fs::path sp(st.srcPath);
        fs::path base = sp.has_parent_path() ? sp.parent_path() : fs::temp_directory_path(ec);
        const fs::path dir = base / ("OtoMad_" + sp.stem().string());
        fs::create_directories(dir, ec);

        struct Entry { std::shared_ptr<Pcm> pcm; std::string path; };
        std::map<std::tuple<int, int, long long>, Entry> cache;

        for (size_t i = 0; i < notes.size(); ++i)
        {
            const MidiNote& nt = notes[i];
            const float startSec = nt.startBeat * 60.0f / bpm;
            const float lenSec = std::max(0.02f, nt.lengthBeats * 60.0f / bpm);
            const double semis = static_cast<double>(nt.pitch) - root + st.tuneCents / 100.0;
            const int q = static_cast<int>(std::lround(semis * 100.0));
            const bool rev = st.reverseEvery > 0 && (static_cast<int>(i) % st.reverseEvery) == st.reverseEvery - 1;
            const long long fit = st.lenMode == 2 ? std::max(1024LL, static_cast<long long>(std::llround(lenSec * sr))) : 0LL;
            const auto key = std::make_tuple(q, rev ? 1 : 0, fit);

            auto it = cache.find(key);
            if (it == cache.end())
            {
                Pcm p = Pitched(region, q / 100.0, st.pitchMode);
                if (rev) extras::ReversePcm(p);
                if (fit > 0) p = FitTo(p, fit);
                char tag[48];
                std::snprintf(tag, sizeof(tag), "_%c%d%s%s", q < 0 ? 'm' : 'p', std::abs(q), rev ? "r" : "", st.pitchMode ? "k" : "t");
                std::string fname = sp.stem().string() + tag + (fit ? "_f" + std::to_string(fit) : std::string()) + ".wav";
                const std::string full = (dir / fname).string();
                if (!extras::SaveWavF32(full, p, sr)) { res.error = "Could not write WAV: " + full; return res; }
                Entry e;
                e.pcm = std::make_shared<Pcm>(std::move(p));
                e.path = full;
                it = cache.emplace(key, std::move(e)).first;
                ++res.unique;
            }
            const Entry& e = it->second;
            const float pcmSec = static_cast<float>(e.pcm->size() / 2) / static_cast<float>(sr);

            float dur = pcmSec;
            if (st.lenMode == 1) dur = std::min(lenSec, pcmSec);
            if (st.mono && st.lenMode != 2)
            {
                size_t j = i + 1;
                while (j < notes.size() && notes[j].startBeat <= nt.startBeat + 0.0001f) ++j;
                if (j < notes.size())
                {
                    const float gap = (notes[j].startBeat - nt.startBeat) * 60.0f / bpm;
                    if (gap < dur) dur = std::max(0.02f, gap);
                }
            }

            float g = 1.0f;
            if (st.autoVol)
            {
                float rms = 0.0f, peak = 0.0f;
                MeasureRaw(e.pcm->data(), std::min(e.pcm->size() / 2, static_cast<size_t>(dur * sr)), rms, peak);
                g = GainFor(rms, peak, st.targetDb);
            }
            g *= std::pow(std::max(0.05f, nt.velocity), st.velAmount);

            Baked b;
            b.pcm = e.pcm;
            b.path = e.path;
            char nm[8];
            MidiPitchName(nt.pitch, nm, sizeof(nm));
            b.name = std::string("OTM ") + nm;
            b.start = startSec;
            b.dur = dur;
            b.gain = std::clamp(g, 0.0f, 4.0f);
            const float fade = std::min(st.fadeMs / 1000.0f, dur * 0.5f);
            b.fadeOut = fade;
            b.fadeIn = std::min(0.003f, fade);
            res.clips.push_back(std::move(b));
        }
        return res;
    }

    // ------------------------------------------------------------ auto volume
    template <class Clip>
    inline bool AutoVolumeOne(Clip& c, float targetDb, unsigned sr)
    {
        if (!c.pcm) return false;
        const size_t total = c.pcm->size() / 2;
        const size_t off = std::min(total, static_cast<size_t>(std::max(0.0f, c.sourceOffsetSeconds) * sr));
        const size_t len = std::min(total - off, static_cast<size_t>(std::max(0.0f, c.duration) * sr));
        if (len == 0) return false;
        float rms = 0.0f, peak = 0.0f;
        MeasureRaw(c.pcm->data() + off * 2, len, rms, peak);
        c.gain = GainFor(rms, peak, targetDb);
        return true;
    }

    template <class Clip>
    inline int AutoVolumeClips(std::vector<std::unique_ptr<Clip>>& clips, int track, float targetDb, unsigned sr)
    {
        int n = 0;
        for (auto& c : clips)
            if (c && c->trackIndex == track && AutoVolumeOne(*c, targetDb, sr)) ++n;
        return n;
    }

    // ------------------------------------------------------------ UI
    template <class Clip, class BuildWave, class AddTrack, class Stop>
    inline bool DrawOtoMadUi(State& st,
        std::vector<std::unique_ptr<Clip>>& clips, Clip*& selectedClip,
        std::vector<MidiNote>& notes, std::vector<std::string>& tracks, int& selectedTrack,
        std::vector<bool>& trackMuted, float bpm, unsigned sr,
        BuildWave buildWave, AddTrack addTrack, Stop stop, std::string& msg)
    {
        bool changed = false;
        char nm[8];

        ImGui::TextColored(ImVec4(0.95f, 0.62f, 0.20f, 1.0f), "OTOMAD STUDIO");
        ImGui::TextDisabled("Sample + MIDI notes -> pitched audio clips on an 'OtoMad' track.");

        // ---- 1. source
        ImGui::SeparatorText("1. Source sample");
        if (st.srcPcm) ImGui::Text("%s  (%.2f s)", st.srcName.c_str(), st.srcDur);
        else ImGui::TextDisabled("Select an audio clip in the Timeline, then press the button.");
        if (ImGui::Button("Use selected clip as source", ImVec2(-1, 28)))
        {
            if (selectedClip && selectedClip->pcm)
            {
                st.srcPcm = selectedClip->pcm;
                st.srcPath = selectedClip->path;
                st.srcName = selectedClip->name;
                st.srcOffset = selectedClip->sourceOffsetSeconds;
                st.srcDur = selectedClip->duration;
                st.detectedMidi = DetectMidi(Region(st, sr), sr, &st.detectConf);
                if (st.detectedMidi >= 0.0f) st.rootNote = std::clamp(static_cast<int>(std::lround(st.detectedMidi)), 12, 108);
                msg = "OtoMad: source set.";
            }
            else msg = "Select an audio clip in the Timeline first.";
        }
        if (st.srcPcm)
        {
            if (st.detectedMidi >= 0.0f)
            {
                const int rn = static_cast<int>(std::lround(st.detectedMidi));
                MidiPitchName(std::clamp(rn, 0, 127), nm, sizeof(nm));
                ImGui::Text("Detected root: %s (%+.0f cents)  confidence %.0f%%", nm, (st.detectedMidi - rn) * 100.0f, st.detectConf * 100.0f);
                if (st.detectConf < 0.6f) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Low confidence (voice / noise?). Use Manual root.");
            }
            else ImGui::TextDisabled("Pitch not detected (too short / unpitched). Use Manual root.");
        }

        // ---- 2. options
        ImGui::SeparatorText("2. Pitch & length");
        static const char* rootModes[] = { "Auto (detected)", "Manual" };
        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("Root", &st.rootMode, rootModes, 2);
        if (st.rootMode == 1)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderInt("##root", &st.rootNote, 12, 108);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", MidiPitchName(st.rootNote, nm, sizeof(nm)));
        }
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Fine tune (cents)", &st.tuneCents, -100.0f, 100.0f, "%+.0f");
        static const char* pitchModes[] = { "Tape (pitch + speed)", "Keep length (WSOLA)" };
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("Pitch method", &st.pitchMode, pitchModes, 2);
        static const char* lenModes[] = { "Natural (whole sample)", "Trim to note length", "Fit to note (stretch)" };
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("Length", &st.lenMode, lenModes, 3);
        ImGui::Checkbox("Mono: cut at next note", &st.mono);

        // ---- 3. dynamics
        ImGui::SeparatorText("3. Volume");
        ImGui::Checkbox("Auto volume (RMS match)", &st.autoVol);
        if (st.autoVol)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("##tdb", &st.targetDb, -30.0f, -3.0f, "%.0f dB");
        }
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Velocity amount", &st.velAmount, 0.0f, 2.0f, "%.2f");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Fade out (ms)", &st.fadeMs, 0.0f, 50.0f, "%.0f");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderInt("Reverse every N notes (0=off)", &st.reverseEvery, 0, 8);
        ImGui::Checkbox("Mute the note track after baking", &st.muteNotes);

        // ---- 4. bake
        ImGui::SeparatorText("4. Bake");
        const bool trackOk = selectedTrack >= 0 && selectedTrack < static_cast<int>(tracks.size());
        int cnt = 0;
        if (trackOk) for (const auto& n : notes) if (n.trackIndex == selectedTrack) ++cnt;
        ImGui::Text("Note track: %s  (%d notes)", trackOk ? tracks[selectedTrack].c_str() : "-", cnt);
        if (ImGui::Button("BAKE NOTES -> OTOMAD CLIPS", ImVec2(-1, 36)))
        {
            if (!trackOk) msg = "Select a track first.";
            else
            {
                const int noteTrack = selectedTrack;
                std::vector<MidiNote> sel;
                for (const auto& n : notes) if (n.trackIndex == noteTrack) sel.push_back(n);
                std::stable_sort(sel.begin(), sel.end(), [](const MidiNote& a, const MidiNote& b)
                    { return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch; });

                stop();
                BakeResult r = Bake(st, sel, bpm, sr);
                if (!r.error.empty()) msg = r.error;
                else
                {
                    int dest = -1;
                    for (int i = 0; i < static_cast<int>(tracks.size()); ++i) if (tracks[i] == "OtoMad") dest = i;
                    if (dest < 0)
                    {
                        addTrack();
                        dest = static_cast<int>(tracks.size()) - 1;
                        tracks[dest] = "OtoMad";
                    }
                    selectedClip = nullptr;
                    clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const std::unique_ptr<Clip>& c)
                        { return c && c->trackIndex == dest && c->name.rfind("OTM ", 0) == 0; }), clips.end());
                    for (auto& b : r.clips)
                    {
                        auto c = std::make_unique<Clip>();
                        c->name = b.name;
                        c->path = b.path;
                        c->pcm = b.pcm;
                        c->sampleRate = sr;
                        c->startSeconds = b.start;
                        c->duration = b.dur;
                        c->sourceOffsetSeconds = 0.0f;
                        c->trackIndex = dest;
                        c->gain = b.gain;
                        c->fadeInSeconds = b.fadeIn;
                        c->fadeOutSeconds = b.fadeOut;
                        buildWave(*c);
                        clips.push_back(std::move(c));
                    }
                    if (st.muteNotes && dest != noteTrack && noteTrack < static_cast<int>(trackMuted.size()))
                        trackMuted[noteTrack] = true;
                    selectedTrack = noteTrack;
                    msg = "OtoMad: baked " + std::to_string(r.clips.size()) + " clips (" + std::to_string(r.unique) + " pitched samples) imported";
                    changed = true;
                }
            }
        }

        // ---- 5. clip tools
        ImGui::SeparatorText("Selected clip tools");
        if (!selectedClip || !selectedClip->pcm)
            ImGui::TextDisabled("Select an audio clip in the Timeline.");
        else
        {
            ImGui::TextWrapped("Clip: %s", selectedClip->name.c_str());
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Semitones", &st.clipSemis, -24.0f, 24.0f, "%+.1f");
            if (ImGui::Button("Pitch (keep length)"))
            {
                const double s = st.clipSemis;
                if (std::fabs(s) < 0.001) msg = "Set a pitch amount first.";
                else if (extras::EditClipPcm(*selectedClip, sr, "_pk", [s](std::vector<float>& b) { b = Pitched(b, s, 1); }))
                {
                    buildWave(*selectedClip); changed = true;
                }
                else msg = "Pitch processing failed.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Pitch (tape)"))
            {
                const double s = st.clipSemis;
                if (std::fabs(s) < 0.001) msg = "Set a pitch amount first.";
                else if (extras::EditClipPcm(*selectedClip, sr, "_pt", [s](std::vector<float>& b) { b = Pitched(b, s, 0); }))
                {
                    buildWave(*selectedClip); changed = true;
                }
                else msg = "Pitch processing failed.";
            }
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Beats", &st.stretchBeats, 0.05f, 0.0625f, 128.0f, "%.3f");
            ImGui::SameLine();
            if (ImGui::Button("Stretch to beats (keep pitch)"))
            {
                const long long frames = std::llround(static_cast<double>(st.stretchBeats) * 60.0 / bpm * sr);
                if (extras::EditClipPcm(*selectedClip, sr, "_st", [frames](std::vector<float>& b) { b = FitTo(b, frames); }))
                {
                    buildWave(*selectedClip); changed = true;
                }
                else msg = "Stretch failed.";
            }
            if (ImGui::Button("Auto volume (this clip)"))
            {
                if (AutoVolumeOne(*selectedClip, st.targetDb, sr)) { msg = "Clip volume adjusted."; changed = true; }
            }
        }
        if (trackOk && ImGui::Button("Auto volume: all clips on selected track", ImVec2(-1, 0)))
        {
            const int k = AutoVolumeClips(clips, selectedTrack, st.targetDb, sr);
            msg = "Auto volume applied to " + std::to_string(k) + " clips.";
            changed = k > 0;
        }
        return changed;
    }
}
