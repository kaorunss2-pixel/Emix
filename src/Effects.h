#pragma once
// EasyMix: built-in effects (streaming-safe).
//  - Every effect processes interleaved stereo float in blocks of any size.
//  - State (filters, delay lines, envelopes, reverb tanks...) lives in the node,
//    so the result does not depend on how the audio is split into blocks.
//  - Time based effects (LFO, fades) use the absolute project frame position that
//    the caller passes in, so seeking / looping stays consistent.
#include <imgui.h>
#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <cstdint>

#include "Equalizer.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Equalizer.h"   // Biquad

enum class EffectType
{
    Gain, Pan, StereoWidth, Mono, StereoSwap, PhaseInvert,
    LowPass, HighPass, BandPass, Notch, LowShelf, HighShelf,
    ParametricEQ, BassBoost, TrebleBoost, Presence, Air,
    Compressor, Limiter, Gate, Expander, DeEsser, TransientShaper,
    Saturator, SoftClip, HardClip, Overdrive, Distortion, Fuzz,
    BitCrusher, Downsample, Tape, Vinyl, Exciter, Noise,
    Delay, PingPongDelay, Slapback, Echo, Reverb, RoomReverb,
    HallReverb, PlateReverb, SpringReverb, Chorus, Flanger, Phaser,
    Tremolo, Vibrato, RingMod, AutoPan, FadeIn, FadeOut,
    Normalize, Reverse, Telephone, Radio, LoFi, StereoEnhancer
};

inline const char* const EffectNames[] =
{
    "Gain", "Pan", "Stereo Width", "Mono", "Stereo Swap", "Phase Invert",
    "Low Pass", "High Pass", "Band Pass", "Notch", "Low Shelf", "High Shelf",
    "Parametric EQ", "Bass Boost", "Treble Boost", "Presence", "Air",
    "Compressor", "Limiter", "Gate", "Expander", "De-Esser", "Transient Shaper",
    "Saturator", "Soft Clip", "Hard Clip", "Overdrive", "Distortion", "Fuzz",
    "Bit Crusher", "Downsample", "Tape", "Vinyl", "Exciter", "Noise",
    "Delay", "Ping Pong Delay", "Slapback", "Echo", "Reverb", "Room Reverb",
    "Hall Reverb", "Plate Reverb", "Spring Reverb", "Chorus", "Flanger", "Phaser",
    "Tremolo", "Vibrato", "Ring Mod", "Auto Pan", "Fade In", "Fade Out",
    "Normalize", "Reverse", "Telephone", "Radio", "Lo-Fi", "Stereo Enhancer"
};

static constexpr int EffectCount =
static_cast<int>(sizeof(EffectNames) / sizeof(EffectNames[0]));

// Effects whose "Wet" slider means "effect level" (default 0.35); all others default to 1.0
inline bool EffectIsTimeBased(EffectType t)
{
    return t >= EffectType::Delay && t <= EffectType::Phaser;
}

// ------------------------------------------------------------------
// Schroeder / Freeverb style reverb tank (4 combs + 2 allpass per channel)
// ------------------------------------------------------------------
struct ReverbState
{
    static constexpr int Combs = 4;

    std::vector<float> comb[2][Combs];
    size_t combPos[2][Combs] = {};
    float combLp[2][Combs] = {};

    std::vector<float> ap[2][2];
    size_t apPos[2][2] = {};

    float scale = 0.0f;
    unsigned rate = 0;

    void clear()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < Combs; ++i)
            {
                comb[c][i].clear();
                combPos[c][i] = 0;
                combLp[c][i] = 0.0f;
            }
            for (int j = 0; j < 2; ++j)
            {
                ap[c][j].clear();
                apPos[c][j] = 0;
            }
        }
        scale = 0.0f;
        rate = 0;
    }
};

struct EffectNode
{
    int id = 0;
    EffectType type = EffectType::Gain;
    ImVec2 position = ImVec2(220.0f, 100.0f);
    float amount = 0.5f;
    float wet = 1.0f;
    float frequency = 1200.0f;
    float time = 0.25f;
    float feedback = 0.25f;
    bool enabled = true;

    // ---- runtime state (not saved) ----
    float stateL = 0.0f;
    float stateR = 0.0f;
    float envelope = 0.0f;
    float env2 = 0.0f;
    float gainState = 1.0f;
    unsigned long long sampleCounter = 0;   // kept for compatibility, unused
    std::vector<float> delayBuffer;         // interleaved stereo frames
    size_t delayPosition = 0;               // in frames
    int revHalf = 0;

    Biquad f1L, f1R, f2L, f2R;
    float apState[2][6] = {};
    float fbL = 0.0f, fbR = 0.0f;
    unsigned rng = 0x12345678u;
    ReverbState rv;
    unsigned visitStamp = 0;
};

struct EffectLink
{
    int from = 0;
    int to = 0;
};

struct EffectGraph
{
    std::vector<EffectNode> nodes;
    std::vector<EffectLink> links = { {1, 2} };
    int nextId = 3;
    int selectedNode = -1;
    int pendingConnection = -1;
    int movingNode = -1;
    ImVec2 dragOffset = ImVec2(0, 0);
    unsigned stampCounter = 0;
};

inline void ResetEffectNodeState(EffectNode& n)
{
    n.stateL = n.stateR = 0.0f;
    n.envelope = 0.0f;
    n.env2 = 0.0f;
    n.gainState = 1.0f;
    n.sampleCounter = 0;
    n.delayBuffer.clear();
    n.delayPosition = 0;
    n.revHalf = 0;
    n.f1L.reset(); n.f1R.reset(); n.f2L.reset(); n.f2R.reset();
    for (auto& a : n.apState) for (float& v : a) v = 0.0f;
    n.fbL = n.fbR = 0.0f;
    n.rng = 0x12345678u;
    n.rv.clear();
}

inline void ResetEffectGraph(EffectGraph& graph)
{
    for (auto& node : graph.nodes)
        ResetEffectNodeState(node);
}

// ------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------
namespace fxdetail
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr double kTwoPi = 6.283185307179586476925286766559;

    inline float DbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
    inline float LinToDb(float x) { return 20.0f * std::log10(std::max(x, 1.0e-6f)); }

    enum class FilterKind { LP, HP, BP, Notch, LowShelf, HighShelf, Peak };

    inline void SetFilter(Biquad& l, Biquad& r, FilterKind k, float sr,
        float f, float q, float gainDb)
    {
        switch (k)
        {
        case FilterKind::LP:        l.setLowPass(sr, f, q); break;
        case FilterKind::HP:        l.setHighPass(sr, f, q); break;
        case FilterKind::BP:        l.setBandPass(sr, f, q); break;
        case FilterKind::Notch:     l.setNotch(sr, f, q); break;
        case FilterKind::LowShelf:  l.setLowShelf(sr, f, gainDb, q); break;
        case FilterKind::HighShelf: l.setHighShelf(sr, f, gainDb, q); break;
        case FilterKind::Peak:      l.setPeaking(sr, f, gainDb, q); break;
        }
        r.copyCoeffsFrom(l);
    }

    inline float Rand01(unsigned& state)
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0xFFFFFF) / 16777216.0f;
    }

    inline float ReadFrac(const std::vector<float>& buf, size_t cap,
        size_t wpos, float delayFrames, int ch)
    {
        float rp = static_cast<float>(wpos) - delayFrames;
        const float fcap = static_cast<float>(cap);
        while (rp < 0.0f) rp += fcap;
        while (rp >= fcap) rp -= fcap;
        size_t i0 = static_cast<size_t>(rp);
        const float fr = rp - static_cast<float>(i0);
        if (i0 >= cap) i0 = 0;
        size_t i1 = i0 + 1;
        if (i1 >= cap) i1 = 0;
        return buf[i0 * 2 + ch] * (1.0f - fr) + buf[i1 * 2 + ch] * fr;
    }

    inline void EnsureReverb(ReverbState& rv, float scale, unsigned sr)
    {
        if (rv.scale == scale && rv.rate == sr && !rv.comb[0][0].empty())
            return;

        static const int combBase[ReverbState::Combs] = { 1116, 1188, 1277, 1356 };
        static const int apBase[2] = { 556, 441 };
        const float k = static_cast<float>(sr) / 44100.0f;

        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < ReverbState::Combs; ++i)
            {
                size_t len = std::max<size_t>(8,
                    static_cast<size_t>((combBase[i] + c * 23) * k * scale));
                rv.comb[c][i].assign(len, 0.0f);
                rv.combPos[c][i] = 0;
                rv.combLp[c][i] = 0.0f;
            }
            for (int j = 0; j < 2; ++j)
            {
                size_t len = std::max<size_t>(8,
                    static_cast<size_t>((apBase[j] + c * 11) * k));
                rv.ap[c][j].assign(len, 0.0f);
                rv.apPos[c][j] = 0;
            }
        }
        rv.scale = scale;
        rv.rate = sr;
    }
}

// ------------------------------------------------------------------
// One effect. samples = interleaved stereo, frames = number of stereo frames.
// pos      = absolute project frame of samples[0]
// endFrame = last frame of the project content (used by Fade Out), 0 = unknown
// ------------------------------------------------------------------
inline void ProcessEffectNode(
    EffectNode& node,
    float* s,
    size_t frames,
    unsigned sr,
    unsigned long long pos,
    unsigned long long endFrame)
{
    using namespace fxdetail;

    if (!node.enabled || !s || frames == 0 || sr == 0)
        return;

    const float fsr = static_cast<float>(sr);
    const float wet = std::clamp(node.wet, 0.0f, 1.0f);
    const float amount = std::clamp(node.amount, 0.0f, 1.0f);
    const float fb = std::clamp(node.feedback, 0.0f, 0.95f);

    auto mixv = [&](float dry, float proc) { return dry * (1.0f - wet) + proc * wet; };
    auto lfo = [&](size_t i, double rate, double phaseOffset) -> float
        {
            const double t = static_cast<double>(pos + i) / static_cast<double>(sr);
            return static_cast<float>(std::sin(kTwoPi * rate * t + phaseOffset));
        };

    switch (node.type)
    {
        // ---------------------------------------------------------- utility
    case EffectType::Gain:
    {
        const float g = amount * 2.0f;   // 0.5 = unity
        for (size_t i = 0; i < frames * 2; ++i) s[i] *= g;
        break;
    }
    case EffectType::Pan:
    case EffectType::AutoPan:
    {
        for (size_t i = 0; i < frames; ++i)
        {
            const float p = node.type == EffectType::Pan
                ? amount * 2.0f - 1.0f
                : lfo(i, 0.5, 0.0) * amount;
            const float angle = (p + 1.0f) * 0.25f * kPi;   // equal power
            s[i * 2] *= std::cos(angle) * 1.41421356f;
            s[i * 2 + 1] *= std::sin(angle) * 1.41421356f;
        }
        break;
    }
    case EffectType::Mono:
    case EffectType::StereoSwap:
    case EffectType::PhaseInvert:
    {
        for (size_t i = 0; i < frames; ++i)
        {
            float& l = s[i * 2];
            float& r = s[i * 2 + 1];
            if (node.type == EffectType::Mono)
                l = r = (l + r) * 0.5f;
            else if (node.type == EffectType::StereoSwap)
                std::swap(l, r);
            else
                l = -l, r = -r;
        }
        break;
    }
    case EffectType::StereoWidth:
    case EffectType::StereoEnhancer:
    {
        const float width = node.type == EffectType::StereoWidth
            ? amount * 2.0f
            : 1.0f + amount * 2.0f;
        for (size_t i = 0; i < frames; ++i)
        {
            const float mid = (s[i * 2] + s[i * 2 + 1]) * 0.5f;
            const float side = (s[i * 2] - s[i * 2 + 1]) * 0.5f * width;
            s[i * 2] = mid + side;
            s[i * 2 + 1] = mid - side;
        }
        break;
    }

    // ------------------------------------------------- real biquad filters
    case EffectType::LowPass:
    case EffectType::HighPass:
    case EffectType::BandPass:
    case EffectType::Notch:
    case EffectType::LowShelf:
    case EffectType::HighShelf:
    case EffectType::ParametricEQ:
    case EffectType::BassBoost:
    case EffectType::TrebleBoost:
    case EffectType::Presence:
    case EffectType::Air:
    {
        const float f = std::clamp(node.frequency, 20.0f, fsr * 0.45f);
        const float q = 0.707f + fb * 8.0f;                 // Feedback slider = resonance
        const float gdb = (amount - 0.5f) * 36.0f;          // +-18 dB
        switch (node.type)
        {
        case EffectType::LowPass:      SetFilter(node.f1L, node.f1R, FilterKind::LP, fsr, f, q, 0); break;
        case EffectType::HighPass:     SetFilter(node.f1L, node.f1R, FilterKind::HP, fsr, f, q, 0); break;
        case EffectType::BandPass:     SetFilter(node.f1L, node.f1R, FilterKind::BP, fsr, f, q, 0); break;
        case EffectType::Notch:        SetFilter(node.f1L, node.f1R, FilterKind::Notch, fsr, f, q, 0); break;
        case EffectType::LowShelf:     SetFilter(node.f1L, node.f1R, FilterKind::LowShelf, fsr, f, 0.707f, gdb); break;
        case EffectType::HighShelf:    SetFilter(node.f1L, node.f1R, FilterKind::HighShelf, fsr, f, 0.707f, gdb); break;
        case EffectType::ParametricEQ: SetFilter(node.f1L, node.f1R, FilterKind::Peak, fsr, f, 0.5f + fb * 6.0f, gdb); break;
        case EffectType::BassBoost:    SetFilter(node.f1L, node.f1R, FilterKind::LowShelf, fsr, 120.0f, 0.707f, amount * 15.0f); break;
        case EffectType::TrebleBoost:  SetFilter(node.f1L, node.f1R, FilterKind::HighShelf, fsr, 6000.0f, 0.707f, amount * 15.0f); break;
        case EffectType::Presence:     SetFilter(node.f1L, node.f1R, FilterKind::Peak, fsr, 4000.0f, 1.0f, amount * 10.0f); break;
        default:                       SetFilter(node.f1L, node.f1R, FilterKind::HighShelf, fsr, 12000.0f, 0.707f, amount * 12.0f); break;
        }
        for (size_t i = 0; i < frames; ++i)
        {
            const float inL = s[i * 2], inR = s[i * 2 + 1];
            s[i * 2] = mixv(inL, node.f1L.process(inL));
            s[i * 2 + 1] = mixv(inR, node.f1R.process(inR));
        }
        break;
    }
    case EffectType::Exciter:
    {
        SetFilter(node.f1L, node.f1R, FilterKind::HP, fsr, 3500.0f, 0.707f, 0);
        const float drive = 1.0f + amount * 6.0f;
        for (size_t i = 0; i < frames; ++i)
        {
            const float inL = s[i * 2], inR = s[i * 2 + 1];
            const float hL = std::tanh(node.f1L.process(inL) * drive);
            const float hR = std::tanh(node.f1R.process(inR) * drive);
            s[i * 2] = inL + hL * amount * 0.8f * wet;
            s[i * 2 + 1] = inR + hR * amount * 0.8f * wet;
        }
        break;
    }

    // --------------------------------------------------------- dynamics
    case EffectType::Compressor:
    case EffectType::Limiter:
    case EffectType::Gate:
    case EffectType::Expander:
    case EffectType::DeEsser:
    {
        const EffectType t = node.type;
        float att = 0.005f, rel = 0.12f;
        float thrDb = -12.0f, ratio = 4.0f, makeupDb = 0.0f;

        if (t == EffectType::Compressor)
        {
            att = 0.005f; rel = 0.12f;
            thrDb = -2.0f - amount * 28.0f;
            ratio = 2.0f + amount * 10.0f;
            makeupDb = -thrDb * (1.0f - 1.0f / ratio) * 0.5f;
        }
        else if (t == EffectType::Limiter)
        {
            att = 0.0005f; rel = 0.08f;
            thrDb = -1.0f - amount * 11.0f;
            ratio = 20.0f;
        }
        else if (t == EffectType::Gate)
        {
            att = 0.001f; rel = 0.05f;
            thrDb = -70.0f + amount * 55.0f;
        }
        else if (t == EffectType::Expander)
        {
            att = 0.003f; rel = 0.1f;
            thrDb = -50.0f + amount * 40.0f;
        }
        else // DeEsser
        {
            att = 0.001f; rel = 0.05f;
            thrDb = -12.0f - amount * 24.0f;
            ratio = 5.0f;
            SetFilter(node.f1L, node.f1R, FilterKind::HP, fsr, 6000.0f, 0.707f, 0);
        }

        const float atkC = std::exp(-1.0f / (att * fsr));
        const float relC = std::exp(-1.0f / (rel * fsr));
        const float openC = 1.0f - std::exp(-1.0f / (0.002f * fsr));
        const float closeC = 1.0f - std::exp(-1.0f / (0.05f * fsr));
        float env = node.envelope;
        float gs = node.gainState;

        for (size_t i = 0; i < frames; ++i)
        {
            const float l = s[i * 2], r = s[i * 2 + 1];
            float hL = 0.0f, hR = 0.0f;
            float det = std::max(std::fabs(l), std::fabs(r));
            if (t == EffectType::DeEsser)
            {
                hL = node.f1L.process(l);
                hR = node.f1R.process(r);
                det = std::max(std::fabs(hL), std::fabs(hR));
            }

            env = det > env ? atkC * env + (1.0f - atkC) * det
                : relC * env + (1.0f - relC) * det;
            const float envDb = LinToDb(env);

            float g = 1.0f;
            if (t == EffectType::Compressor || t == EffectType::Limiter)
            {
                const float over = envDb - thrDb;
                const float gDb = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
                g = DbToLin(gDb + makeupDb);
            }
            else if (t == EffectType::Gate)
            {
                const float target = envDb > thrDb ? 1.0f : 0.0f;
                gs += (target - gs) * (target > gs ? openC : closeC);
                g = gs;
            }
            else if (t == EffectType::Expander)
            {
                g = DbToLin(std::min(0.0f, envDb - thrDb));
            }
            else // DeEsser: reduce only the sibilant band
            {
                const float over = envDb - thrDb;
                const float gDb = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
                g = DbToLin(gDb);
                s[i * 2] = l - hL + hL * g;
                s[i * 2 + 1] = r - hR + hR * g;
                continue;
            }
            s[i * 2] = l * g;
            s[i * 2 + 1] = r * g;
        }
        node.envelope = env;
        node.gainState = gs;
        break;
    }
    case EffectType::TransientShaper:
    {
        const float aF = std::exp(-1.0f / (0.001f * fsr)), rF = std::exp(-1.0f / (0.02f * fsr));
        const float aS = std::exp(-1.0f / (0.03f * fsr)), rS = std::exp(-1.0f / (0.15f * fsr));
        float fast = node.envelope, slow = node.env2;
        const float shape = amount * 2.0f - 1.0f;   // + = more attack, - = less attack
        for (size_t i = 0; i < frames; ++i)
        {
            const float det = std::max(std::fabs(s[i * 2]), std::fabs(s[i * 2 + 1]));
            fast = det > fast ? aF * fast + (1.0f - aF) * det : rF * fast + (1.0f - rF) * det;
            slow = det > slow ? aS * slow + (1.0f - aS) * det : rS * slow + (1.0f - rS) * det;
            const float trans = std::clamp((fast - slow) / (slow + 1.0e-4f), 0.0f, 1.0f);
            const float g = std::clamp(1.0f + shape * trans * 1.5f, 0.2f, 3.0f);
            s[i * 2] *= g;
            s[i * 2 + 1] *= g;
        }
        node.envelope = fast;
        node.env2 = slow;
        break;
    }

    // ------------------------------------------------------- saturation
    case EffectType::Saturator:
    case EffectType::SoftClip:
    case EffectType::HardClip:
    case EffectType::Overdrive:
    case EffectType::Distortion:
    case EffectType::Fuzz:
    case EffectType::Tape:
    case EffectType::Telephone:
    case EffectType::Radio:
    case EffectType::LoFi:
    {
        const EffectType t = node.type;
        const float drive = 1.0f + amount * 18.0f;

        if (t == EffectType::Tape)
            SetFilter(node.f1L, node.f1R, FilterKind::LP, fsr, 14000.0f, 0.707f, 0);
        else if (t == EffectType::Telephone)
        {
            SetFilter(node.f1L, node.f1R, FilterKind::HP, fsr, 300.0f, 0.9f, 0);
            SetFilter(node.f2L, node.f2R, FilterKind::LP, fsr, 3400.0f, 0.9f, 0);
        }
        else if (t == EffectType::Radio)
        {
            SetFilter(node.f1L, node.f1R, FilterKind::HP, fsr, 500.0f, 0.9f, 0);
            SetFilter(node.f2L, node.f2R, FilterKind::LP, fsr, 4500.0f, 0.9f, 0);
        }
        else if (t == EffectType::LoFi)
            SetFilter(node.f1L, node.f1R, FilterKind::LP, fsr, 7000.0f, 0.707f, 0);

        const float lofiLevels = std::pow(2.0f, 12.0f - amount * 8.0f - 1.0f);

        auto shape = [&](float x, Biquad* f1, Biquad* f2) -> float
            {
                switch (t)
                {
                case EffectType::SoftClip: { const float y = x * drive; return y / (1.0f + std::fabs(y)); }
                case EffectType::HardClip:   return std::clamp(x * drive, -1.0f, 1.0f);
                case EffectType::Overdrive:  return std::tanh(x * drive * 0.5f);
                case EffectType::Distortion: return std::clamp(x * drive, -0.7f, 0.7f) / 0.7f;
                case EffectType::Fuzz:       return std::tanh(x * drive * 2.0f + 0.1f) - std::tanh(0.1f);
                case EffectType::Tape:       return f1->process(std::tanh(x * (1.0f + amount * 3.0f)) * 0.9f);
                case EffectType::Telephone:  return std::tanh(f2->process(f1->process(x)) * (1.0f + amount * 4.0f)) * 0.9f;
                case EffectType::Radio:      return std::tanh(f2->process(f1->process(x)) * (1.0f + amount * 6.0f)) * 0.8f;
                case EffectType::LoFi:       return f1->process(std::round(x * lofiLevels) / lofiLevels);
                default:                     return std::tanh(x * drive);
                }
            };

        for (size_t i = 0; i < frames; ++i)
        {
            const float inL = s[i * 2], inR = s[i * 2 + 1];
            const float pL = shape(inL, &node.f1L, &node.f2L);
            const float pR = shape(inR, &node.f1R, &node.f2R);
            s[i * 2] = mixv(inL, pL);
            s[i * 2 + 1] = mixv(inR, pR);
        }
        break;
    }
    case EffectType::BitCrusher:
    case EffectType::Downsample:
    {
        const int bits = std::clamp(static_cast<int>(16.0f - amount * 14.0f), 2, 16);
        const float levels = static_cast<float>(1 << (bits - 1));
        const unsigned long long hold =
            static_cast<unsigned long long>(std::clamp(1.0f + amount * 30.0f, 1.0f, 31.0f));
        for (size_t i = 0; i < frames; ++i)
        {
            if (node.type == EffectType::BitCrusher)
            {
                s[i * 2] = std::round(s[i * 2] * levels) / levels;
                s[i * 2 + 1] = std::round(s[i * 2 + 1] * levels) / levels;
            }
            else
            {
                if ((pos + i) % hold == 0)
                {
                    node.stateL = s[i * 2];
                    node.stateR = s[i * 2 + 1];
                }
                s[i * 2] = node.stateL;
                s[i * 2 + 1] = node.stateR;
            }
        }
        break;
    }

    // ----------------------------------------------------------- delays
    case EffectType::Delay:
    case EffectType::PingPongDelay:
    case EffectType::Slapback:
    case EffectType::Echo:
    {
        const size_t cap = static_cast<size_t>(3.0f * fsr) + 8;
        if (node.delayBuffer.size() != cap * 2)
        {
            node.delayBuffer.assign(cap * 2, 0.0f);
            node.delayPosition = 0;
        }
        float seconds = node.time;
        float feedback = fb;
        if (node.type == EffectType::Slapback) { seconds = 0.09f; feedback = 0.1f; }
        const size_t dFrames = std::clamp<size_t>(
            static_cast<size_t>(seconds * fsr), 1, cap - 1);
        const float damp = node.type == EffectType::Echo ? 0.35f : 0.0f;

        size_t w = node.delayPosition;
        for (size_t i = 0; i < frames; ++i)
        {
            const size_t rp = (w + cap - dFrames) % cap;
            float dL = node.delayBuffer[rp * 2];
            float dR = node.delayBuffer[rp * 2 + 1];
            if (damp > 0.0f)
            {
                node.stateL += (dL - node.stateL) * (1.0f - damp);
                node.stateR += (dR - node.stateR) * (1.0f - damp);
                dL = node.stateL;
                dR = node.stateR;
            }

            const float inL = s[i * 2], inR = s[i * 2 + 1];
            float wL, wR;
            if (node.type == EffectType::PingPongDelay)
            {
                const float mono = (inL + inR) * 0.5f;
                wL = mono + dR * feedback;
                wR = dL * feedback;
            }
            else
            {
                wL = inL + dL * feedback;
                wR = inR + dR * feedback;
            }
            node.delayBuffer[w * 2] = wL;
            node.delayBuffer[w * 2 + 1] = wR;

            s[i * 2] = inL + dL * wet;
            s[i * 2 + 1] = inR + dR * wet;

            if (++w >= cap) w = 0;
        }
        node.delayPosition = w;
        break;
    }

    // ----------------------------------------------- modulated delays
    case EffectType::Chorus:
    case EffectType::Flanger:
    case EffectType::Vibrato:
    {
        const size_t cap = static_cast<size_t>(0.2f * fsr) + 8;
        if (node.delayBuffer.size() != cap * 2)
        {
            node.delayBuffer.assign(cap * 2, 0.0f);
            node.delayPosition = 0;
        }
        float baseMs, depthMs, rate, feedback = 0.0f, mix;
        if (node.type == EffectType::Chorus)
        {
            baseMs = 18.0f; depthMs = 3.0f + amount * 5.0f; rate = 0.5 + amount * 1.5; mix = wet * 0.5f;
        }
        else if (node.type == EffectType::Flanger)
        {
            baseMs = 2.5f; depthMs = 1.0f + amount * 3.0f; rate = 0.2 + amount * 0.6; mix = wet * 0.5f;
            feedback = fb;
        }
        else
        {
            baseMs = 6.0f; depthMs = 0.5f + amount * 4.0f; rate = 5.0; mix = wet;
        }

        size_t w = node.delayPosition;
        for (size_t i = 0; i < frames; ++i)
        {
            const float lfoL = lfo(i, rate, 0.0);
            const float lfoR = lfo(i, rate, node.type == EffectType::Vibrato ? 0.0 : 1.5707963);
            const float dlL = std::max(1.0f, (baseMs + depthMs * lfoL) * 0.001f * fsr);
            const float dlR = std::max(1.0f, (baseMs + depthMs * lfoR) * 0.001f * fsr);
            const float dL = ReadFrac(node.delayBuffer, cap, w, dlL, 0);
            const float dR = ReadFrac(node.delayBuffer, cap, w, dlR, 1);

            const float inL = s[i * 2], inR = s[i * 2 + 1];
            node.delayBuffer[w * 2] = inL + dL * feedback;
            node.delayBuffer[w * 2 + 1] = inR + dR * feedback;

            s[i * 2] = inL * (1.0f - mix) + dL * mix;
            s[i * 2 + 1] = inR * (1.0f - mix) + dR * mix;

            if (++w >= cap) w = 0;
        }
        node.delayPosition = w;
        break;
    }
    case EffectType::Phaser:
    {
        const double rate = 0.2 + amount * 1.3;
        const float mix = wet * 0.5f;
        for (size_t i = 0; i < frames; ++i)
        {
            const float m = 0.5f + 0.5f * lfo(i, rate, 0.0);
            const float fc = 300.0f * std::pow(10.0f, m);   // 300 Hz .. 3 kHz sweep
            const float tn = std::tan(kPi * std::min(fc, fsr * 0.45f) / fsr);
            const float a = (tn - 1.0f) / (tn + 1.0f);

            const float inL = s[i * 2], inR = s[i * 2 + 1];
            float xL = inL + node.fbL * fb;
            float xR = inR + node.fbR * fb;
            for (int st = 0; st < 4; ++st)
            {
                float yL = a * xL + node.apState[0][st];
                node.apState[0][st] = xL - a * yL;
                xL = yL;
                float yR = a * xR + node.apState[1][st];
                node.apState[1][st] = xR - a * yR;
                xR = yR;
            }
            node.fbL = xL;
            node.fbR = xR;
            s[i * 2] = inL * (1.0f - mix) + xL * mix;
            s[i * 2 + 1] = inR * (1.0f - mix) + xR * mix;
        }
        break;
    }

    // ----------------------------------------------------------- reverb
    case EffectType::Reverb:
    case EffectType::RoomReverb:
    case EffectType::HallReverb:
    case EffectType::PlateReverb:
    case EffectType::SpringReverb:
    {
        float scale = 1.0f, baseFb = 0.80f, damp = 0.25f;
        switch (node.type)
        {
        case EffectType::RoomReverb:   scale = 0.55f; baseFb = 0.72f; damp = 0.40f; break;
        case EffectType::HallReverb:   scale = 1.60f; baseFb = 0.84f; damp = 0.25f; break;
        case EffectType::PlateReverb:  scale = 1.00f; baseFb = 0.80f; damp = 0.12f; break;
        case EffectType::SpringReverb: scale = 0.80f; baseFb = 0.78f; damp = 0.30f; break;
        default: break;
        }
        const float combFb = std::clamp(baseFb + (amount - 0.5f) * 0.2f, 0.3f, 0.96f);
        EnsureReverb(node.rv, scale, sr);
        ReverbState& rv = node.rv;

        for (size_t i = 0; i < frames; ++i)
        {
            const float inL = s[i * 2], inR = s[i * 2 + 1];
            const float in = (inL + inR) * 0.5f * 0.05f;
            float tail[2];
            for (int c = 0; c < 2; ++c)
            {
                float sum = 0.0f;
                for (int k = 0; k < ReverbState::Combs; ++k)
                {
                    std::vector<float>& buf = rv.comb[c][k];
                    size_t& p = rv.combPos[c][k];
                    const float y = buf[p];
                    rv.combLp[c][k] = y * (1.0f - damp) + rv.combLp[c][k] * damp;
                    buf[p] = in + rv.combLp[c][k] * combFb;
                    if (++p >= buf.size()) p = 0;
                    sum += y;
                }
                for (int j = 0; j < 2; ++j)
                {
                    std::vector<float>& buf = rv.ap[c][j];
                    size_t& p = rv.apPos[c][j];
                    const float bo = buf[p];
                    const float out = -sum + bo;
                    buf[p] = sum + bo * 0.5f;
                    if (++p >= buf.size()) p = 0;
                    sum = out;
                }
                tail[c] = sum;
            }
            s[i * 2] = inL * (1.0f - 0.5f * wet) + tail[0] * wet * 2.0f;
            s[i * 2 + 1] = inR * (1.0f - 0.5f * wet) + tail[1] * wet * 2.0f;
        }
        break;
    }

    // ------------------------------------------------------ modulation
    case EffectType::Tremolo:
    {
        const double rate = 3.0 + amount * 8.0;
        for (size_t i = 0; i < frames; ++i)
        {
            const float mod = 1.0f - amount * 0.8f * (0.5f + 0.5f * lfo(i, rate, 0.0));
            s[i * 2] *= mod;
            s[i * 2 + 1] *= mod;
        }
        break;
    }
    case EffectType::RingMod:
    {
        const double rate = std::clamp(node.frequency, 20.0f, 5000.0f);
        for (size_t i = 0; i < frames; ++i)
        {
            const float mod = lfo(i, rate, 0.0);
            s[i * 2] = mixv(s[i * 2], s[i * 2] * mod);
            s[i * 2 + 1] = mixv(s[i * 2 + 1], s[i * 2 + 1] * mod);
        }
        break;
    }

    // ------------------------------------------------------ time based
    case EffectType::FadeIn:
    case EffectType::FadeOut:
    {
        const double fadeFrames = std::max(1.0, static_cast<double>(node.time) * sr);
        for (size_t i = 0; i < frames; ++i)
        {
            const unsigned long long p = pos + i;
            float g = 1.0f;
            if (node.type == EffectType::FadeIn)
                g = static_cast<float>(std::min(1.0, static_cast<double>(p) / fadeFrames));
            else if (endFrame > 0)
            {
                const double remaining = endFrame > p ? static_cast<double>(endFrame - p) : 0.0;
                g = static_cast<float>(std::min(1.0, remaining / fadeFrames));
            }
            s[i * 2] *= g;
            s[i * 2 + 1] *= g;
        }
        break;
    }
    case EffectType::Normalize:   // automatic level (peak follower), works in realtime
    {
        const float decay = std::exp(-1.0f / (1.5f * fsr));
        float env = node.envelope;
        float g = node.gainState;
        for (size_t i = 0; i < frames; ++i)
        {
            const float level = std::max(std::fabs(s[i * 2]), std::fabs(s[i * 2 + 1]));
            env = std::max(level, env * decay);
            const float target = std::min(8.0f, 0.98f / std::max(env, 0.05f));
            g += (target - g) * (target < g ? 0.05f : 0.0002f);
            s[i * 2] *= g;
            s[i * 2 + 1] *= g;
        }
        node.envelope = env;
        node.gainState = g;
        break;
    }
    case EffectType::Reverse:     // realtime reverse: reverses the previous "Time" seconds
    {
        const size_t N = std::max<size_t>(1, static_cast<size_t>(node.time * fsr));
        if (node.delayBuffer.size() != N * 4)
        {
            node.delayBuffer.assign(N * 4, 0.0f);
            node.delayPosition = 0;
            node.revHalf = 0;
        }
        size_t wp = node.delayPosition;
        int half = node.revHalf;
        for (size_t i = 0; i < frames; ++i)
        {
            const size_t wi = (static_cast<size_t>(half) * N + wp) * 2;
            const size_t ri = (static_cast<size_t>(1 - half) * N + (N - 1 - wp)) * 2;
            const float inL = s[i * 2], inR = s[i * 2 + 1];
            const float rL = node.delayBuffer[ri];
            const float rR = node.delayBuffer[ri + 1];
            node.delayBuffer[wi] = inL;
            node.delayBuffer[wi + 1] = inR;
            s[i * 2] = mixv(inL, rL);
            s[i * 2 + 1] = mixv(inR, rR);
            if (++wp >= N) { wp = 0; half ^= 1; }
        }
        node.delayPosition = wp;
        node.revHalf = half;
        break;
    }
    case EffectType::Noise:
    case EffectType::Vinyl:
    {
        for (size_t i = 0; i < frames; ++i)
        {
            if (node.type == EffectType::Noise)
            {
                const float n = Rand01(node.rng) * 2.0f - 1.0f;
                s[i * 2] += n * amount * 0.08f;
                s[i * 2 + 1] += (Rand01(node.rng) * 2.0f - 1.0f) * amount * 0.08f;
            }
            else
            {
                const float hiss = (Rand01(node.rng) * 2.0f - 1.0f) * 0.004f * (0.5f + amount);
                float crackle = 0.0f;
                if (Rand01(node.rng) < 0.0003f * (0.2f + amount * 2.0f))
                    crackle = (Rand01(node.rng) * 2.0f - 1.0f) * 0.25f;
                s[i * 2] += hiss + crackle;
                s[i * 2 + 1] += hiss + crackle;
            }
        }
        break;
    }
    }
}

// Run the whole chain TRACK IN (id 1) -> ... -> TRACK OUT (id 2).
inline void ProcessEffectGraph(
    EffectGraph& graph,
    float* samples,
    size_t frames,
    unsigned sampleRate,
    unsigned long long pos,
    unsigned long long endFrame)
{
    if (graph.nodes.empty())
        return;

    const unsigned stamp = ++graph.stampCounter;
    int current = 1;

    for (size_t step = 0; step < graph.nodes.size() + 1; ++step)
    {
        auto link = std::find_if(
            graph.links.begin(), graph.links.end(),
            [current](const EffectLink& l) { return l.from == current; });

        if (link == graph.links.end() || link->to == 2)
            break;

        auto node = std::find_if(
            graph.nodes.begin(), graph.nodes.end(),
            [&](const EffectNode& n) { return n.id == link->to; });

        if (node == graph.nodes.end() || node->visitStamp == stamp)
            break;

        node->visitStamp = stamp;
        ProcessEffectNode(*node, samples, frames, sampleRate, pos, endFrame);
        current = node->id;
    }
}