#pragma once
// EasyMix Studio: built-in polyphonic synthesizer with a large preset bank.
//   - melodic presets : 2 oscillators + unison + sub + noise + FM + ADSR + SVF filter + LFOs + pitch env
//   - drum kits       : GM-mapped synthesized drums (kick, snare, hats, toms, cymbals, percussion ...)
// Usage (audio thread):  eng.SetRate(sr); eng.NoteOn(setup, pitch, vel); eng.NoteOff(pitch); eng.Render(out, frames);
// Render() ADDS to the interleaved stereo buffer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace synth
{
    enum Wave { Sine = 0, Saw, Square, Triangle, Noise };

    struct Patch
    {
        const char* name = "";
        const char* category = "";
        // oscillators
        int w1 = Saw;    float l1 = 1.0f; float semi1 = 0.0f;
        int w2 = Saw;    float l2 = 0.0f; float semi2 = 0.0f; float det2 = 0.0f;
        int uni = 1;     float uniCents = 0.0f;
        float sub = 0.0f, noise = 0.0f, pw = 0.5f;
        float pwmRate = 0.0f, pwmDepth = 0.0f;
        // FM (osc1 is phase modulated by a sine modulator)
        float fmRatio = 1.0f, fmIndex = 0.0f, fmDecay = 0.5f;
        // amp envelope (seconds)
        float a = 0.005f, d = 0.1f, s = 1.0f, r = 0.12f;
        // filter
        float cutoff = 20000.0f, res = 0.0f, envOct = 0.0f, fDecay = 0.3f, fSus = 0.0f, keyTrack = 0.0f, velOct = 0.0f;
        float fltLfoRate = 0.0f, fltLfoOct = 0.0f;
        // modulation
        float vibRate = 0.0f, vibCents = 0.0f, vibDelay = 0.0f;
        float tremRate = 0.0f, tremDepth = 0.0f;
        float pitchEnv = 0.0f, pitchEnvDecay = 0.05f;   // semitones at note start, decay seconds
        // output
        float gain = 0.25f, spread = 0.0f, drive = 0.0f;
        // drums
        int kit = -1;

        Patch() {}
        Patch(const char* n, const char* c) : name(n), category(c) {}

        Patch& O1(int w, float l, float semi = 0.0f) { w1 = w; l1 = l; semi1 = semi; return *this; }
        Patch& O2(int w, float l, float semi = 0.0f, float det = 0.0f) { w2 = w; l2 = l; semi2 = semi; det2 = det; return *this; }
        Patch& Uni(int n, float cents) { uni = std::clamp(n, 1, 5); uniCents = cents; return *this; }
        Patch& Sub(float v) { sub = v; return *this; }
        Patch& Nz(float v) { noise = v; return *this; }
        Patch& PW(float v, float rate = 0.0f, float depth = 0.0f) { pw = v; pwmRate = rate; pwmDepth = depth; return *this; }
        Patch& FM(float ratio, float index, float decay) { fmRatio = ratio; fmIndex = index; fmDecay = decay; return *this; }
        Patch& Env(float A, float D, float S, float R) { a = A; d = D; s = S; r = R; return *this; }
        Patch& Flt(float c, float q, float eo = 0.0f, float fd = 0.3f, float fs = 0.0f, float key = 0.0f, float vo = 0.0f)
        {
            cutoff = c; res = q; envOct = eo; fDecay = fd; fSus = fs; keyTrack = key; velOct = vo; return *this;
        }
        Patch& FltLfo(float rate, float oct) { fltLfoRate = rate; fltLfoOct = oct; return *this; }
        Patch& Vib(float rate, float cents, float delay = 0.0f) { vibRate = rate; vibCents = cents; vibDelay = delay; return *this; }
        Patch& Trem(float rate, float depth) { tremRate = rate; tremDepth = depth; return *this; }
        Patch& PEnv(float semis, float decay) { pitchEnv = semis; pitchEnvDecay = decay; return *this; }
        Patch& G(float v) { gain = v; return *this; }
        Patch& Spread(float v) { spread = v; return *this; }
        Patch& Drive(float v) { drive = v; return *this; }
        Patch& Kit(int k) { kit = k; return *this; }
    };

    struct KitDef { float pitch, decay, noise, drive; };

    inline const KitDef& KitParams(int k)
    {
        static const KitDef kits[] =
        {
            { 1.00f, 1.00f, 1.00f, 0.00f },  // 0 Acoustic
            { 0.90f, 1.25f, 1.10f, 0.15f },  // 1 Rock
            { 1.15f, 0.80f, 0.70f, 0.00f },  // 2 Jazz
            { 0.75f, 1.90f, 0.60f, 0.20f },  // 3 808
            { 1.00f, 1.00f, 1.00f, 0.30f },  // 4 909
            { 1.10f, 0.70f, 0.90f, 0.40f },  // 5 Electro
            { 0.95f, 0.90f, 1.20f, 0.70f },  // 6 Lo-Fi
            { 0.70f, 2.20f, 0.80f, 0.40f },  // 7 Trap
        };
        return kits[std::clamp(k, 0, 7)];
    }

    inline const std::vector<Patch>& Patches()
    {
        static const std::vector<Patch> bank = []()
            {
                std::vector<Patch> v;
                auto add = [&](const Patch& p) { v.push_back(p); };

                // ------------------------------------------------------------ Default
                add(Patch("Classic (Default)", "Default").O1(Sine, 0.7f).O2(Saw, 0.3f).Env(0.005f, 0.1f, 1.0f, 0.12f).G(0.22f));

                // ------------------------------------------------------------ Piano / Keys
                add(Patch("Grand Piano", "Piano & Keys").O1(Saw, 0.6f).O2(Triangle, 0.5f, 12, 3).Env(0.002f, 2.6f, 0.0f, 0.35f)
                    .Flt(1800, 0.1f, 2.5f, 0.6f, 0.2f, 0.6f, 0.5f).G(0.30f).Spread(0.5f));
                add(Patch("Bright Piano", "Piano & Keys").O1(Saw, 0.7f).O2(Square, 0.25f, 12, 4).Env(0.001f, 2.2f, 0.0f, 0.3f)
                    .Flt(3500, 0.1f, 2.0f, 0.5f, 0.3f, 0.6f, 0.6f).G(0.28f).Spread(0.5f));
                add(Patch("Upright Piano", "Piano & Keys").O1(Triangle, 0.7f).O2(Saw, 0.3f, 0, 5).Env(0.002f, 1.8f, 0.0f, 0.25f)
                    .Flt(1200, 0.15f, 2.0f, 0.4f, 0.15f, 0.5f, 0.5f).Drive(0.15f).G(0.32f).Spread(0.4f));
                add(Patch("Honky-tonk Piano", "Piano & Keys").O1(Saw, 0.55f).O2(Saw, 0.55f, 0, 14).Env(0.002f, 1.8f, 0.0f, 0.25f)
                    .Flt(2400, 0.1f, 2.0f, 0.4f, 0.2f, 0.5f, 0.5f).G(0.26f).Spread(0.4f));
                add(Patch("Electric Piano (Rhodes)", "Piano & Keys").O1(Sine, 1.0f).O2(Sine, 0.2f, 12).FM(1.0f, 1.8f, 0.5f)
                    .Env(0.003f, 2.2f, 0.0f, 0.25f).Trem(4.5f, 0.08f).G(0.26f).Spread(0.4f));
                add(Patch("DX Piano (FM)", "Piano & Keys").O1(Sine, 1.0f).FM(14.0f, 0.7f, 0.12f).O2(Sine, 0.5f, 0)
                    .Env(0.002f, 1.6f, 0.0f, 0.2f).G(0.24f).Spread(0.3f));
                add(Patch("Wurlitzer", "Piano & Keys").O1(Triangle, 0.8f).O2(Square, 0.2f, 0).Env(0.004f, 1.4f, 0.1f, 0.2f)
                    .Flt(2500, 0.1f, 1.0f, 0.3f, 0.3f, 0.4f).Trem(5.5f, 0.15f).Drive(0.2f).G(0.26f));
                add(Patch("Clavinet", "Piano & Keys").O1(Square, 1.0f).PW(0.25f).Env(0.001f, 0.6f, 0.0f, 0.06f)
                    .Flt(1800, 0.25f, 2.5f, 0.15f, 0.1f, 0.7f, 0.5f).G(0.24f));
                add(Patch("Harpsichord", "Piano & Keys").O1(Square, 0.7f).PW(0.15f).O2(Saw, 0.5f, 12).Env(0.001f, 0.9f, 0.0f, 0.05f)
                    .Flt(5000, 0.1f, 1.0f, 0.1f, 0.3f, 0.5f).G(0.2f));
                add(Patch("Celesta", "Piano & Keys").O1(Sine, 1.0f, 12).FM(4.0f, 0.8f, 0.3f).Env(0.001f, 1.4f, 0.0f, 0.3f).G(0.24f));
                add(Patch("Music Box", "Piano & Keys").O1(Sine, 1.0f, 12).FM(5.0f, 0.5f, 0.2f).O2(Sine, 0.3f, 24).Env(0.001f, 1.0f, 0.0f, 0.4f).G(0.22f));

                // ------------------------------------------------------------ Organ
                add(Patch("Drawbar Organ", "Organ").O1(Sine, 1.0f).O2(Sine, 0.5f, 12).Sub(0.5f).Env(0.005f, 0.05f, 1.0f, 0.04f)
                    .Trem(6.0f, 0.06f).G(0.20f));
                add(Patch("Rock Organ", "Organ").O1(Square, 0.8f).O2(Square, 0.4f, 12).Sub(0.3f).Env(0.003f, 0.05f, 1.0f, 0.05f)
                    .Drive(0.6f).Trem(6.7f, 0.1f).G(0.17f));
                add(Patch("Church Organ", "Organ").O1(Saw, 0.5f).O2(Saw, 0.4f, 12).Sub(0.6f).Flt(2500, 0.05f).Env(0.05f, 0.1f, 1.0f, 0.4f)
                    .G(0.17f).Spread(0.5f));
                add(Patch("Reed Organ", "Organ").O1(Square, 0.7f).PW(0.35f).O2(Saw, 0.3f).Flt(1800, 0.1f).Env(0.03f, 0.1f, 1.0f, 0.1f).G(0.2f));
                add(Patch("Accordion", "Organ").O1(Saw, 0.6f).O2(Saw, 0.6f, 0, 14).Flt(3000, 0.1f).Env(0.03f, 0.1f, 1.0f, 0.1f)
                    .Vib(5.5f, 6, 0.2f).G(0.18f).Spread(0.4f));
                add(Patch("Harmonica", "Organ").O1(Square, 0.7f).PW(0.4f).O2(Saw, 0.4f, 0, 8).Nz(0.02f).Flt(2600, 0.2f).Env(0.04f, 0.1f, 0.9f, 0.1f)
                    .Vib(5.0f, 8, 0.3f).G(0.18f));

                // ------------------------------------------------------------ Bass
                add(Patch("Sub Bass", "Bass").O1(Sine, 1.0f).Env(0.005f, 0.1f, 1.0f, 0.1f).G(0.45f));
                add(Patch("Synth Bass", "Bass").O1(Saw, 1.0f).Sub(0.5f).Env(0.002f, 0.3f, 0.7f, 0.1f).Flt(500, 0.35f, 2.5f, 0.2f, 0.2f, 0.3f).G(0.3f));
                add(Patch("Acid Bass", "Bass").O1(Saw, 1.0f).Env(0.002f, 0.3f, 0.8f, 0.05f).Flt(300, 0.8f, 3.0f, 0.18f, 0.0f, 0.2f).G(0.22f));
                add(Patch("Moog Bass", "Bass").O1(Saw, 1.0f).O2(Square, 0.8f, 0, 6).Env(0.003f, 0.3f, 0.8f, 0.1f).Flt(400, 0.3f, 3.0f, 0.25f, 0.1f, 0.3f).G(0.26f));
                add(Patch("Fingered Bass", "Bass").O1(Triangle, 1.0f).O2(Saw, 0.3f).Env(0.003f, 1.2f, 0.15f, 0.15f).Flt(700, 0.1f, 1.5f, 0.3f, 0.3f, 0.3f, 0.4f).G(0.4f));
                add(Patch("Slap Bass", "Bass").O1(Square, 1.0f).PW(0.3f).Env(0.001f, 0.3f, 0.3f, 0.1f).Flt(1200, 0.2f, 3.0f, 0.08f, 0.2f, 0.3f, 0.5f).G(0.3f));
                add(Patch("Pick Bass", "Bass").O1(Saw, 1.0f).Env(0.001f, 0.6f, 0.4f, 0.1f).Flt(800, 0.15f, 2.0f, 0.1f, 0.4f, 0.3f, 0.4f).G(0.32f));
                add(Patch("Fretless Bass", "Bass").O1(Triangle, 0.8f).O2(Sine, 0.5f).Vib(4.5f, 5, 0.3f).Env(0.01f, 0.5f, 0.6f, 0.15f).Flt(900, 0.1f).G(0.4f));
                add(Patch("Wobble Bass", "Bass").O1(Saw, 1.0f).Uni(2, 12).Sub(0.4f).Env(0.003f, 0.2f, 1.0f, 0.1f).Flt(450, 0.6f).FltLfo(3.0f, 2.0f).G(0.26f));
                add(Patch("Reese Bass", "Bass").O1(Saw, 1.0f).Uni(3, 18).Sub(0.4f).Env(0.01f, 0.2f, 1.0f, 0.15f).Flt(700, 0.15f).G(0.24f).Spread(0.3f));
                add(Patch("Square Bass", "Bass").O1(Square, 0.9f).Sub(0.5f).Env(0.003f, 0.2f, 0.9f, 0.08f).Flt(600, 0.1f, 1.2f, 0.2f, 0.4f).G(0.3f));
                add(Patch("808 Bass", "Bass").O1(Sine, 1.0f).PEnv(7, 0.06f).Env(0.001f, 1.5f, 0.7f, 0.2f).Drive(0.3f).G(0.45f));
                add(Patch("Distorted Bass", "Bass").O1(Saw, 1.0f).Sub(0.3f).Drive(0.8f).Flt(900, 0.2f, 1.0f, 0.2f, 0.4f).Env(0.002f, 0.3f, 0.9f, 0.08f).G(0.22f));

                // ------------------------------------------------------------ Lead
                add(Patch("Saw Lead", "Lead").O1(Saw, 1.0f).Uni(3, 14).Flt(4000, 0.2f).Env(0.005f, 0.1f, 0.8f, 0.15f).Vib(5.5f, 8, 0.25f).G(0.2f).Spread(0.5f));
                add(Patch("Square Lead", "Lead").O1(Square, 0.8f).O2(Square, 0.4f, 12).Env(0.005f, 0.05f, 0.9f, 0.1f).Vib(5.5f, 8, 0.25f).Flt(5000, 0.1f).G(0.2f));
                add(Patch("PWM Lead", "Lead").O1(Square, 1.0f).PW(0.5f, 0.8f, 0.3f).Uni(2, 8).Env(0.005f, 0.1f, 0.85f, 0.15f).Flt(5000, 0.15f).Vib(5.5f, 7, 0.3f).G(0.2f).Spread(0.4f));
                add(Patch("Supersaw Lead", "Lead").O1(Saw, 1.0f).Uni(5, 22).Flt(6500, 0.1f).Env(0.005f, 0.2f, 0.8f, 0.2f).G(0.16f).Spread(0.8f));
                add(Patch("Hard Lead", "Lead").O1(Saw, 0.8f).O2(Saw, 0.6f, 7).Flt(3500, 0.3f, 1.0f, 0.2f, 0.6f).Env(0.003f, 0.1f, 0.9f, 0.1f).G(0.2f));
                add(Patch("Mono Lead 80s", "Lead").O1(Saw, 0.8f).O2(Square, 0.5f, 0, 7).Flt(3000, 0.3f, 1.0f, 0.3f, 0.6f).Env(0.005f, 0.2f, 0.85f, 0.15f).Vib(5.0f, 10, 0.3f).G(0.2f));
                add(Patch("Soft Lead", "Lead").O1(Triangle, 0.8f).O2(Sine, 0.5f, 12).Env(0.02f, 0.1f, 0.9f, 0.2f).Vib(5.0f, 14, 0.2f).G(0.26f));
                add(Patch("Flute Lead", "Lead").O1(Sine, 1.0f).O2(Sine, 0.15f, 12).Nz(0.03f).Env(0.06f, 0.05f, 0.9f, 0.15f).Vib(5.0f, 7, 0.3f).G(0.3f));
                add(Patch("Whistle", "Lead").O1(Sine, 1.0f).Nz(0.01f).Env(0.05f, 0.05f, 0.9f, 0.15f).Vib(5.5f, 12, 0.4f).G(0.3f));
                add(Patch("Theremin", "Lead").O1(Sine, 1.0f).O2(Sine, 0.1f, 12).Env(0.1f, 0.1f, 1.0f, 0.2f).Vib(5.0f, 25, 0.1f).G(0.3f));
                add(Patch("Chiptune Square", "Lead").O1(Square, 1.0f).PW(0.25f).Env(0.001f, 0.01f, 1.0f, 0.03f).Vib(6.0f, 10, 0.15f).G(0.16f));
                add(Patch("Chiptune 50%", "Lead").O1(Square, 1.0f).PW(0.5f).Env(0.001f, 0.01f, 1.0f, 0.03f).G(0.16f));
                add(Patch("Chiptune Triangle", "Lead").O1(Triangle, 1.0f).Env(0.001f, 0.01f, 1.0f, 0.02f).G(0.3f));
                add(Patch("Arp Pluck", "Lead").O1(Saw, 1.0f).Env(0.001f, 0.25f, 0.0f, 0.1f).Flt(3000, 0.2f, 2.5f, 0.2f, 0.0f, 0.5f).G(0.26f));
                add(Patch("Laser Zap", "Lead").O1(Saw, 1.0f).PEnv(24, 0.08f).Env(0.001f, 0.25f, 0.0f, 0.05f).Flt(6000, 0.2f).G(0.24f));

                // ------------------------------------------------------------ Pad
                add(Patch("Warm Pad", "Pad").O1(Saw, 1.0f).Uni(3, 10).O2(Triangle, 0.4f).Flt(1500, 0.1f).Env(0.8f, 0.5f, 0.85f, 1.2f).G(0.17f).Spread(0.8f));
                add(Patch("Soft Pad", "Pad").O1(Triangle, 1.0f).O2(Sine, 0.5f, 12, 4).Flt(2500, 0.05f).Env(0.6f, 0.4f, 0.9f, 1.0f).G(0.22f).Spread(0.7f));
                add(Patch("Glass Pad", "Pad").O1(Sine, 1.0f).FM(2.0f, 0.5f, 3.0f).O2(Sine, 0.5f, 12).Env(0.5f, 0.5f, 0.9f, 1.4f).G(0.22f).Spread(0.8f));
                add(Patch("Choir Pad (Ahh)", "Pad").O1(Saw, 1.0f).Uni(3, 9).Nz(0.02f).Flt(1100, 0.35f).Env(0.4f, 0.2f, 0.9f, 0.6f).Vib(5.0f, 8, 0.4f).G(0.18f).Spread(0.7f));
                add(Patch("Strings Pad", "Pad").O1(Saw, 1.0f).Uni(3, 14).Flt(3500, 0.05f).Env(0.35f, 0.3f, 0.9f, 0.5f).G(0.17f).Spread(0.9f));
                add(Patch("Sweep Pad", "Pad").O1(Saw, 1.0f).Uni(3, 12).Flt(400, 0.3f, 3.0f, 2.5f, 0.6f).Env(0.6f, 0.5f, 0.9f, 1.0f).G(0.17f).Spread(0.8f));
                add(Patch("Analog Pad", "Pad").O1(Saw, 0.7f).O2(Square, 0.6f, 0, 7).PW(0.5f, 0.3f, 0.25f).Flt(2000, 0.15f).Env(0.5f, 0.5f, 0.85f, 0.9f).G(0.17f).Spread(0.7f));
                add(Patch("PWM Pad", "Pad").O1(Square, 1.0f).PW(0.5f, 0.4f, 0.35f).Uni(2, 10).Flt(2200, 0.1f).Env(0.6f, 0.4f, 0.9f, 1.0f).G(0.16f).Spread(0.8f));
                add(Patch("Dark Pad", "Pad").O1(Saw, 1.0f).Uni(2, 12).Flt(700, 0.2f).Env(1.0f, 0.5f, 0.9f, 1.4f).G(0.2f).Spread(0.6f));
                add(Patch("Space Pad", "Pad").O1(Saw, 1.0f).Uni(5, 25).Flt(1500, 0.2f).FltLfo(0.2f, 1.2f).Env(1.2f, 0.5f, 0.9f, 2.5f).G(0.14f).Spread(1.0f));
                add(Patch("Ice Pad", "Pad").O1(Sine, 1.0f, 12).FM(3.5f, 0.8f, 2.0f).O2(Triangle, 0.3f).Env(0.8f, 0.5f, 0.85f, 1.8f).G(0.2f).Spread(0.9f));
                add(Patch("Evolving Pad", "Pad").O1(Square, 0.8f).PW(0.5f, 0.15f, 0.4f).O2(Saw, 0.5f, 0, 9).Flt(1200, 0.3f).FltLfo(0.1f, 1.5f).Env(1.5f, 0.5f, 0.9f, 2.0f).G(0.16f).Spread(0.9f));
                add(Patch("Halo Pad", "Pad").O1(Sine, 0.8f).O2(Triangle, 0.5f, 12, 5).Nz(0.02f).Flt(3000, 0.1f).Env(0.8f, 0.5f, 0.9f, 1.5f).G(0.22f).Spread(0.9f));
                add(Patch("Ambient Drone", "Pad").O1(Saw, 0.8f).Uni(5, 30).Sub(0.5f).Flt(600, 0.4f).FltLfo(0.07f, 1.0f).Env(2.5f, 0.5f, 1.0f, 3.0f).G(0.13f).Spread(1.0f));

                // ------------------------------------------------------------ Strings & Ensemble
                add(Patch("String Ensemble", "Strings").O1(Saw, 1.0f).Uni(3, 12).Flt(3800, 0.05f).Env(0.25f, 0.2f, 0.9f, 0.4f).Vib(5.2f, 4, 0.3f).G(0.17f).Spread(0.7f));
                add(Patch("Violin", "Strings").O1(Saw, 1.0f).Flt(3200, 0.1f, 0.5f, 0.2f, 0.8f).Env(0.09f, 0.1f, 0.85f, 0.2f).Vib(5.8f, 14, 0.3f).G(0.2f));
                add(Patch("Viola", "Strings").O1(Saw, 1.0f).Flt(2400, 0.12f, 0.5f, 0.2f, 0.8f).Env(0.1f, 0.1f, 0.85f, 0.2f).Vib(5.5f, 12, 0.3f).G(0.22f));
                add(Patch("Cello", "Strings").O1(Saw, 1.0f).O2(Triangle, 0.4f).Flt(1800, 0.12f, 0.5f, 0.2f, 0.8f).Env(0.12f, 0.1f, 0.85f, 0.25f).Vib(5.0f, 10, 0.3f).G(0.24f));
                add(Patch("Contrabass", "Strings").O1(Saw, 0.8f).Sub(0.5f).Flt(900, 0.1f, 0.5f, 0.2f, 0.8f).Env(0.14f, 0.1f, 0.85f, 0.3f).Vib(4.5f, 8, 0.3f).G(0.3f));
                add(Patch("Pizzicato Strings", "Strings").O1(Saw, 1.0f).Env(0.001f, 0.3f, 0.0f, 0.1f).Flt(2500, 0.1f, 2.0f, 0.08f, 0.1f, 0.4f).G(0.3f));
                add(Patch("Tremolo Strings", "Strings").O1(Saw, 1.0f).Uni(3, 10).Flt(3500, 0.05f).Env(0.1f, 0.2f, 0.9f, 0.3f).Trem(9.0f, 0.5f).G(0.18f).Spread(0.7f));
                add(Patch("Harp", "Strings").O1(Triangle, 1.0f).O2(Sine, 0.3f, 12).Env(0.001f, 1.8f, 0.0f, 0.5f).Flt(3500, 0.05f, 1.5f, 0.3f, 0.2f, 0.4f).G(0.3f).Spread(0.6f));

                // ------------------------------------------------------------ Brass
                add(Patch("Brass Section", "Brass").O1(Saw, 1.0f).Uni(3, 8).Flt(800, 0.1f, 2.0f, 0.25f, 0.7f, 0.3f, 0.5f).Env(0.06f, 0.15f, 0.85f, 0.12f).G(0.2f).Spread(0.5f));
                add(Patch("Trumpet", "Brass").O1(Saw, 1.0f).Flt(1400, 0.2f, 1.8f, 0.12f, 0.6f, 0.3f, 0.5f).Env(0.04f, 0.1f, 0.9f, 0.1f).Vib(5.5f, 6, 0.4f).G(0.22f));
                add(Patch("Trombone", "Brass").O1(Saw, 1.0f).Flt(700, 0.15f, 1.8f, 0.15f, 0.6f, 0.3f, 0.5f).Env(0.06f, 0.1f, 0.9f, 0.12f).Vib(5.0f, 6, 0.4f).G(0.24f));
                add(Patch("French Horn", "Brass").O1(Triangle, 0.8f).O2(Saw, 0.5f).Flt(800, 0.1f, 1.2f, 0.2f, 0.6f, 0.3f).Env(0.1f, 0.15f, 0.85f, 0.2f).Vib(5.0f, 5, 0.4f).G(0.24f));
                add(Patch("Synth Brass", "Brass").O1(Saw, 1.0f).O2(Saw, 0.8f, 0, 10).Flt(600, 0.15f, 2.5f, 0.3f, 0.6f, 0.3f).Env(0.02f, 0.2f, 0.8f, 0.15f).G(0.2f).Spread(0.5f));
                add(Patch("Tuba", "Brass").O1(Saw, 0.8f).Sub(0.4f).Flt(450, 0.1f, 1.5f, 0.2f, 0.6f, 0.3f).Env(0.07f, 0.1f, 0.9f, 0.15f).G(0.3f));

                // ------------------------------------------------------------ Woodwind
                add(Patch("Flute", "Woodwind").O1(Sine, 1.0f).O2(Sine, 0.12f, 12).Nz(0.04f).Env(0.07f, 0.05f, 0.9f, 0.15f).Vib(5.0f, 6, 0.4f).G(0.3f));
                add(Patch("Clarinet", "Woodwind").O1(Square, 1.0f).Nz(0.02f).Flt(2200, 0.15f).Env(0.05f, 0.1f, 0.9f, 0.1f).Vib(5.0f, 4, 0.4f).G(0.2f));
                add(Patch("Oboe", "Woodwind").O1(Saw, 1.0f).Flt(1600, 0.45f).Env(0.04f, 0.1f, 0.9f, 0.1f).Vib(5.5f, 6, 0.3f).G(0.2f));
                add(Patch("Bassoon", "Woodwind").O1(Saw, 1.0f).Flt(700, 0.3f).Env(0.05f, 0.1f, 0.9f, 0.12f).Vib(5.0f, 5, 0.3f).G(0.25f));
                add(Patch("Alto Sax", "Woodwind").O1(Saw, 0.8f).O2(Square, 0.5f).Flt(1800, 0.25f, 0.5f, 0.2f, 0.8f).Nz(0.02f).Env(0.04f, 0.1f, 0.9f, 0.1f).Vib(5.5f, 10, 0.35f).G(0.2f));
                add(Patch("Pan Flute", "Woodwind").O1(Sine, 1.0f).Nz(0.1f).Flt(4000, 0.2f).Env(0.06f, 0.05f, 0.9f, 0.15f).Vib(4.5f, 5, 0.5f).G(0.28f));
                add(Patch("Recorder", "Woodwind").O1(Triangle, 0.8f).O2(Sine, 0.4f, 12).Nz(0.03f).Env(0.04f, 0.05f, 0.9f, 0.1f).Vib(5.0f, 4, 0.4f).G(0.28f));

                // ------------------------------------------------------------ Mallet / Pluck / Bell
                add(Patch("Marimba", "Mallet & Bell").O1(Sine, 1.0f).FM(3.9f, 0.5f, 0.08f).Env(0.001f, 0.7f, 0.0f, 0.15f).G(0.32f));
                add(Patch("Vibraphone", "Mallet & Bell").O1(Sine, 1.0f).FM(4.0f, 0.3f, 1.0f).Env(0.002f, 2.5f, 0.0f, 0.4f).Trem(5.0f, 0.4f).G(0.3f));
                add(Patch("Xylophone", "Mallet & Bell").O1(Sine, 1.0f, 12).FM(3.0f, 0.9f, 0.05f).Env(0.001f, 0.25f, 0.0f, 0.08f).G(0.3f));
                add(Patch("Glockenspiel", "Mallet & Bell").O1(Sine, 1.0f, 24).FM(3.5f, 0.8f, 0.4f).Env(0.001f, 1.2f, 0.0f, 0.3f).G(0.24f));
                add(Patch("Tubular Bell", "Mallet & Bell").O1(Sine, 1.0f).FM(3.5f, 3.0f, 3.0f).Env(0.001f, 4.0f, 0.0f, 1.5f).G(0.22f));
                add(Patch("FM Bell", "Mallet & Bell").O1(Sine, 1.0f).FM(1.41f, 2.2f, 2.0f).Env(0.001f, 3.0f, 0.0f, 1.0f).G(0.22f));
                add(Patch("Kalimba", "Mallet & Bell").O1(Triangle, 1.0f).FM(5.0f, 0.4f, 0.1f).Env(0.001f, 1.0f, 0.0f, 0.25f).G(0.3f));
                add(Patch("Steel Drum", "Mallet & Bell").O1(Sine, 1.0f).FM(2.0f, 1.2f, 0.4f).Env(0.001f, 0.9f, 0.0f, 0.2f).G(0.28f));
                add(Patch("Nylon Guitar", "Guitar & Pluck").O1(Triangle, 1.0f).O2(Saw, 0.3f).Env(0.002f, 1.2f, 0.0f, 0.2f).Flt(1800, 0.1f, 2.0f, 0.25f, 0.2f, 0.5f, 0.4f).G(0.32f));
                add(Patch("Steel Guitar", "Guitar & Pluck").O1(Saw, 1.0f).Env(0.001f, 1.2f, 0.0f, 0.2f).Flt(2800, 0.1f, 2.5f, 0.2f, 0.2f, 0.5f, 0.4f).G(0.26f));
                add(Patch("Muted Guitar", "Guitar & Pluck").O1(Saw, 1.0f).Env(0.001f, 0.2f, 0.0f, 0.06f).Flt(1200, 0.2f, 1.5f, 0.1f, 0.1f, 0.4f).G(0.3f));
                add(Patch("Clean Electric Guitar", "Guitar & Pluck").O1(Saw, 0.8f).O2(Triangle, 0.4f).Env(0.002f, 1.5f, 0.1f, 0.2f).Flt(2200, 0.15f, 1.5f, 0.3f, 0.3f, 0.5f, 0.4f).Drive(0.1f).G(0.26f));
                add(Patch("Distortion Guitar", "Guitar & Pluck").O1(Saw, 0.8f).O2(Square, 0.4f, 0, 6).Env(0.002f, 1.0f, 0.5f, 0.1f).Flt(2500, 0.15f, 1.0f, 0.2f, 0.5f, 0.4f).Drive(1.0f).G(0.16f));
                add(Patch("Banjo", "Guitar & Pluck").O1(Square, 1.0f).PW(0.2f).Env(0.001f, 0.4f, 0.0f, 0.1f).Flt(3500, 0.2f, 1.5f, 0.1f, 0.2f, 0.4f).G(0.22f));
                add(Patch("Sitar", "Guitar & Pluck").O1(Saw, 1.0f).FM(2.0f, 0.5f, 0.5f).Env(0.001f, 2.0f, 0.0f, 0.3f).Flt(2000, 0.5f, 2.0f, 0.6f, 0.2f, 0.5f).Vib(6.0f, 12, 0.2f).G(0.22f));
                add(Patch("Koto", "Guitar & Pluck").O1(Saw, 1.0f).O2(Triangle, 0.4f).PEnv(1.5f, 0.05f).Env(0.001f, 1.0f, 0.0f, 0.2f).Flt(3500, 0.15f, 1.5f, 0.15f, 0.2f, 0.4f).G(0.26f));
                add(Patch("Pluck Synth", "Guitar & Pluck").O1(Saw, 0.8f).O2(Square, 0.5f, 12).Env(0.001f, 0.5f, 0.0f, 0.1f).Flt(2500, 0.2f, 3.0f, 0.15f, 0.0f, 0.5f).G(0.24f));

                // ------------------------------------------------------------ FX
                add(Patch("Wind", "FX").O1(Noise, 1.0f).Flt(800, 0.4f).FltLfo(0.3f, 1.2f).Env(1.0f, 0.5f, 1.0f, 1.5f).G(0.3f).Spread(1.0f));
                add(Patch("Sci-Fi", "FX").O1(Sine, 1.0f).FM(1.41f, 3.0f, 2.0f).Vib(0.8f, 30, 0.0f).Env(0.3f, 0.5f, 0.9f, 1.0f).G(0.2f).Spread(0.8f));
                add(Patch("Alien", "FX").O1(Sine, 0.8f).O2(Triangle, 0.4f, 7).Vib(7.0f, 80, 0.0f).Env(0.1f, 0.2f, 0.9f, 0.4f).G(0.24f));
                add(Patch("Noise Hit", "FX").O1(Noise, 1.0f).Flt(6000, 0.2f, -2.0f, 0.3f, 0.0f).Env(0.001f, 0.4f, 0.0f, 0.1f).G(0.3f));
                add(Patch("Sweep FX", "FX").O1(Saw, 1.0f).Uni(3, 20).Flt(300, 0.6f, 4.0f, 1.5f, 0.0f).Env(0.01f, 1.5f, 0.0f, 0.5f).G(0.16f).Spread(0.8f));
                add(Patch("Pitch Drop", "FX").O1(Sine, 1.0f).PEnv(36, 0.25f).Env(0.001f, 0.8f, 0.0f, 0.1f).G(0.3f));
                add(Patch("Dream Bell Pad", "FX").O1(Sine, 1.0f, 12).FM(3.01f, 1.2f, 3.0f).Env(0.4f, 1.0f, 0.5f, 2.0f).G(0.2f).Spread(1.0f));

                // ------------------------------------------------------------ Drum kits (GM mapping)
                add(Patch("Acoustic Kit", "Drums").Kit(0).G(0.42f));
                add(Patch("Rock Kit", "Drums").Kit(1).G(0.42f));
                add(Patch("Jazz Kit", "Drums").Kit(2).G(0.42f));
                add(Patch("808 Kit", "Drums").Kit(3).G(0.42f));
                add(Patch("909 Kit", "Drums").Kit(4).G(0.42f));
                add(Patch("Electro Kit", "Drums").Kit(5).G(0.42f));
                add(Patch("Lo-Fi Kit", "Drums").Kit(6).G(0.42f));
                add(Patch("Trap Kit", "Drums").Kit(7).G(0.42f));
                return v;
            }();
        return bank;
    }

    inline int FindPreset(const char* name)
    {
        const auto& p = Patches();
        for (int i = 0; i < static_cast<int>(p.size()); ++i)
            if (std::strcmp(p[i].name, name) == 0) return i;
        return 0;
    }

    // per-track settings (saved with the project)
    struct Setup
    {
        int preset = 0;
        float gain = 1.0f;      // 0..2
        float bright = 0.5f;    // 0..1  (0.5 = neutral, +-2 octaves of filter cutoff)
        float attack = 0.5f;    // 0..1  (0.5 = neutral, x0.25 .. x4)
        float release = 0.5f;   // 0..1
    };

    // ===================================================================== voices
    struct Voice
    {
        bool active = false, gate = false;
        int patch = 0, pitch = 60;
        std::uint32_t age = 0;
        float vel = 0.8f, gainMul = 1.0f, cutMul = 1.0f, atkMul = 1.0f, relMul = 1.0f;
        double ph[5] = { 0, 0, 0, 0, 0 };
        double incU[5] = { 0, 0, 0, 0, 0 };
        double ph2 = 0, inc2 = 0, phSub = 0, incSub = 0, phMod = 0, incMod = 0;
        double lfoPh = 0, tremPh = 0, fltLfoPh = 0, pwmPh = 0;
        float env = 0.0f;
        int stage = 0;               // 0 attack 1 decay/sustain 3 release
        float fenv = 1.0f, fmEnv = 1.0f, pEnv = 1.0f;
        float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
        double t = 0.0;
        float panL = 0.7f, panR = 0.7f;
        double baseInc = 0.0;
        float keyMul = 1.0f;
        // drums
        int drumKind = 0;
        float drumFreq = 100.0f, drumDur = 0.3f;
        float hp = 0.0f;
        double dph[6] = { 0, 0, 0, 0, 0, 0 };
        std::uint32_t rng = 12345u;
    };

    inline float PolyBlep(double t, double dt)
    {
        if (t < dt) { t /= dt; return static_cast<float>(t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return static_cast<float>(t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline float NextNoise(std::uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(s)) * (1.0f / 2147483648.0f);
    }

    inline float OscSample(int wave, double ph, double inc, float pw, std::uint32_t& rng)
    {
        switch (wave)
        {
        case Sine: return static_cast<float>(std::sin(6.283185307179586 * ph));
        case Saw: return static_cast<float>(2.0 * ph - 1.0) - PolyBlep(ph, inc);
        case Square:
        {
            float v = ph < pw ? 1.0f : -1.0f;
            v += PolyBlep(ph, inc);
            double p2 = ph + (1.0 - pw);
            if (p2 >= 1.0) p2 -= 1.0;
            v -= PolyBlep(p2, inc);
            return v;
        }
        case Triangle: return static_cast<float>(4.0 * std::fabs(ph - 0.5) - 1.0);
        default: return NextNoise(rng);
        }
    }

    // GM drum map -> kind
    enum DrumKind { DKick, DSnare, DRim, DClap, DHatC, DHatO, DTom, DCrash, DRide, DCowbell, DShaker, DTamb, DClave, DWood, DConga, DPerc };

    inline void MapDrum(int pitch, int& kind, float& freq, float& dur)
    {
        freq = 100.0f; dur = 0.4f;
        switch (pitch)
        {
        case 35: case 36: kind = DKick; dur = 0.8f; break;
        case 37: kind = DRim; dur = 0.1f; break;
        case 38: case 40: kind = DSnare; dur = 0.4f; break;
        case 39: kind = DClap; dur = 0.4f; break;
        case 42: case 44: kind = DHatC; dur = 0.15f; break;
        case 46: kind = DHatO; dur = 0.8f; break;
        case 41: kind = DTom; freq = 80.0f; dur = 0.7f; break;
        case 43: kind = DTom; freq = 100.0f; dur = 0.7f; break;
        case 45: kind = DTom; freq = 125.0f; dur = 0.7f; break;
        case 47: kind = DTom; freq = 155.0f; dur = 0.7f; break;
        case 48: kind = DTom; freq = 185.0f; dur = 0.7f; break;
        case 50: kind = DTom; freq = 220.0f; dur = 0.7f; break;
        case 49: case 52: case 55: case 57: kind = DCrash; dur = 2.0f; break;
        case 51: case 53: case 59: kind = DRide; dur = 1.3f; break;
        case 54: kind = DTamb; dur = 0.35f; break;
        case 56: kind = DCowbell; dur = 0.45f; break;
        case 69: case 70: case 82: kind = DShaker; dur = 0.18f; break;
        case 75: kind = DClave; dur = 0.12f; break;
        case 76: kind = DWood; freq = 1100.0f; dur = 0.12f; break;
        case 77: kind = DWood; freq = 800.0f; dur = 0.12f; break;
        case 60: kind = DConga; freq = 330.0f; dur = 0.3f; break;
        case 61: kind = DConga; freq = 240.0f; dur = 0.3f; break;
        case 62: kind = DConga; freq = 300.0f; dur = 0.25f; break;
        case 63: kind = DConga; freq = 220.0f; dur = 0.35f; break;
        case 64: kind = DConga; freq = 170.0f; dur = 0.4f; break;
        default:
            kind = DPerc;
            freq = 220.0f * std::pow(2.0f, (pitch - 60) / 12.0f);
            dur = 0.25f;
            break;
        }
    }

    // ===================================================================== engine
    struct Engine
    {
        static constexpr int kMaxVoices = 40;
        std::vector<Voice> voices;
        float sr = 44100.0f;
        std::uint32_t counter = 0;

        Engine() : voices(kMaxVoices) {}

        void SetRate(float r) { sr = r > 8000.0f ? r : 44100.0f; }
        bool Active() const { for (const auto& v : voices) if (v.active) return true; return false; }
        void Reset() { for (auto& v : voices) v.active = false; }
        void ReleaseAll() { for (auto& v : voices) if (v.active && v.gate) { v.gate = false; v.stage = 3; } }
        void NoteOff(int pitch)
        {
            for (auto& v : voices)
                if (v.active && v.gate && v.pitch == pitch) { v.gate = false; v.stage = 3; return; }
        }

        void NoteOn(const Setup& su, int pitch, float vel)
        {
            const auto& bank = Patches();
            const int pi = std::clamp(su.preset, 0, static_cast<int>(bank.size()) - 1);
            const Patch& P = bank[pi];

            Voice* slot = nullptr;
            for (auto& v : voices) if (!v.active) { slot = &v; break; }
            if (!slot)
            {
                slot = &voices[0];
                for (auto& v : voices) if (v.age < slot->age) slot = &v;
            }

            Voice& v = *slot;
            v = Voice();
            v.active = true; v.gate = true;
            v.patch = pi; v.pitch = pitch; v.age = ++counter;
            v.vel = std::clamp(vel, 0.0f, 1.0f);
            v.gainMul = std::clamp(su.gain, 0.0f, 2.0f);
            v.cutMul = std::exp2((su.bright - 0.5f) * 4.0f);
            v.atkMul = std::exp2((su.attack - 0.5f) * 4.0f);
            v.relMul = std::exp2((su.release - 0.5f) * 4.0f);
            v.rng = 22695477u * static_cast<std::uint32_t>(counter + 1) + 1u;

            if (P.kit >= 0)
            {
                MapDrum(pitch, v.drumKind, v.drumFreq, v.drumDur);
                const KitDef& kd = KitParams(P.kit);
                v.drumDur *= std::max(0.5f, kd.decay);
                float pan = 0.0f;
                switch (v.drumKind)
                {
                case DHatC: case DHatO: pan = 0.3f; break;
                case DTom: pan = (v.drumFreq - 140.0f) / 200.0f; break;
                case DCrash: pan = -0.3f; break;
                case DRide: pan = 0.35f; break;
                case DShaker: case DTamb: pan = 0.4f; break;
                default: break;
                }
                pan = std::clamp(pan, -0.8f, 0.8f);
                v.panL = std::sqrt(0.5f * (1.0f - pan));
                v.panR = std::sqrt(0.5f * (1.0f + pan));
                if (v.drumKind == DHatC)   // choke open hats
                    for (auto& o : voices)
                        if (o.active && o.patch == pi && o.drumKind == DHatO) { o.t = std::max(o.t, o.drumDur - 0.03); }
                return;
            }

            const double freq = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
            v.baseInc = freq / sr;
            const double m1 = std::pow(2.0, P.semi1 / 12.0);
            for (int i = 0; i < 5; ++i)
            {
                double det = 0.0;
                if (P.uni > 1) det = ((static_cast<double>(i) / (P.uni - 1)) * 2.0 - 1.0) * P.uniCents;
                v.incU[i] = v.baseInc * m1 * std::pow(2.0, det / 1200.0);
                v.ph[i] = (P.uni > 1) ? NextNoise(v.rng) * 0.5 + 0.5 : 0.0;
            }
            v.inc2 = v.baseInc * std::pow(2.0, P.semi2 / 12.0) * std::pow(2.0, P.det2 / 1200.0);
            v.incSub = v.baseInc * 0.5;
            v.incMod = v.baseInc * m1 * P.fmRatio;
            v.keyMul = std::exp2(P.keyTrack * (pitch - 60) / 12.0f);
            const float pan = std::clamp(P.spread * (static_cast<float>(pitch - 60) / 36.0f), -0.9f, 0.9f);
            v.panL = std::sqrt(0.5f * (1.0f - pan));
            v.panR = std::sqrt(0.5f * (1.0f + pan));
            v.fenv = 1.0f; v.fmEnv = 1.0f; v.pEnv = 1.0f;
        }

        // ---------------------------------------------------------------- drum voice
        float DrumSample(Voice& v, const Patch& P)
        {
            const KitDef& kd = KitParams(P.kit);
            const float t = static_cast<float>(v.t);
            const float dec = kd.decay;
            const float pm = kd.pitch;
            float s = 0.0f;
            const float nz = NextNoise(v.rng);
            auto hpf = [&](float x, float c) { v.hp += c * (x - v.hp); return x - v.hp; };
            const double dt = 1.0 / sr;
            auto metal = [&](float base)
                {
                    static const float ratios[6] = { 2.0f, 3.0f, 4.16f, 5.43f, 6.79f, 8.21f };
                    float m = 0.0f;
                    for (int i = 0; i < 6; ++i)
                    {
                        v.dph[i] += base * ratios[i] * dt;
                        if (v.dph[i] >= 1.0) v.dph[i] -= 1.0;
                        m += v.dph[i] < 0.5 ? 1.0f : -1.0f;
                    }
                    return m * (1.0f / 6.0f);
                };

            switch (v.drumKind)
            {
            case DKick:
            {
                const float f = (45.0f + 140.0f * std::exp(-t * 32.0f)) * pm;
                v.dph[0] += f * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                s = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * (7.0f / dec));
                s += nz * std::exp(-t * 400.0f) * 0.25f;
                s *= 1.1f;
                break;
            }
            case DSnare:
            {
                v.dph[0] += 185.0 * pm * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                v.dph[1] += 330.0 * pm * dt; if (v.dph[1] >= 1.0) v.dph[1] -= 1.0;
                const float tone = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * 24.0f / dec)
                    + static_cast<float>(std::sin(6.283185307179586 * v.dph[1])) * 0.5f * std::exp(-t * 30.0f / dec);
                const float noise = hpf(nz, 0.35f) * std::exp(-t * 15.0f / dec) * kd.noise;
                s = tone * 0.6f + noise * 0.8f;
                break;
            }
            case DRim:
            {
                v.dph[0] += 1700.0 * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                s = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * 110.0f) * 0.7f
                    + nz * std::exp(-t * 300.0f) * 0.3f;
                break;
            }
            case DClap:
            {
                float e = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    const float tb = t - 0.011f * b;
                    if (tb >= 0.0f) e = std::max(e, std::exp(-tb * 450.0f));
                }
                if (t > 0.033f) e = std::max(e, std::exp(-(t - 0.033f) * 32.0f / dec));
                s = hpf(nz, 0.25f) * e * kd.noise * 0.9f;
                break;
            }
            case DHatC:
                s = (metal(40.0f * pm) * 0.6f + hpf(nz, 0.4f) * 0.5f) * std::exp(-t * 55.0f / dec);
                s = hpf(s, 0.2f) * 0.8f;
                break;
            case DHatO:
                s = (metal(40.0f * pm) * 0.6f + hpf(nz, 0.4f) * 0.5f) * std::exp(-t * 7.0f / dec);
                s = hpf(s, 0.2f) * 0.7f;
                break;
            case DTom:
            {
                const float f = v.drumFreq * pm * (1.0f + 0.5f * std::exp(-t * 20.0f));
                v.dph[0] += f * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                s = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * 7.0f / dec)
                    + nz * std::exp(-t * 200.0f) * 0.12f;
                break;
            }
            case DCrash:
                s = (hpf(nz, 0.3f) * 0.6f + metal(55.0f) * 0.4f) * std::exp(-t * 2.8f / dec) * 0.7f;
                break;
            case DRide:
                s = (metal(70.0f) * 0.5f + hpf(nz, 0.5f) * 0.2f) * std::exp(-t * 4.0f / dec) * 0.7f;
                break;
            case DCowbell:
            {
                v.dph[0] += 560.0 * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                v.dph[1] += 845.0 * dt; if (v.dph[1] >= 1.0) v.dph[1] -= 1.0;
                s = ((v.dph[0] < 0.5 ? 1.0f : -1.0f) + (v.dph[1] < 0.5 ? 1.0f : -1.0f)) * 0.3f * std::exp(-t * 10.0f);
                break;
            }
            case DShaker:
                s = hpf(nz, 0.4f) * std::min(1.0f, t / 0.012f) * std::exp(-t * 28.0f) * 0.8f;
                break;
            case DTamb:
                s = (hpf(nz, 0.35f) * 0.6f + metal(90.0f) * 0.3f) * std::exp(-t * 14.0f) * 0.8f;
                break;
            case DClave:
                v.dph[0] += 2500.0 * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                s = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * 60.0f);
                break;
            case DWood:
                v.dph[0] += v.drumFreq * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                s = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * 50.0f);
                break;
            case DConga:
            {
                const float f = v.drumFreq * (1.0f + 0.3f * std::exp(-t * 40.0f));
                v.dph[0] += f * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                s = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * 12.0f)
                    + nz * std::exp(-t * 250.0f) * 0.15f;
                break;
            }
            default:
                v.dph[0] += v.drumFreq * dt; if (v.dph[0] >= 1.0) v.dph[0] -= 1.0;
                s = static_cast<float>(std::sin(6.283185307179586 * v.dph[0])) * std::exp(-t * 30.0f);
                break;
            }

            if (kd.drive > 0.0f)
            {
                const float x = s * (1.0f + kd.drive * 3.0f);
                s = x / (1.0f + std::fabs(x)) * (1.0f + kd.drive * 0.8f);
            }
            return s;
        }

        // ---------------------------------------------------------------- render (adds)
        void Render(float* out, int frames)
        {
            if (frames <= 0) return;
            const auto& bank = Patches();
            const double dt = 1.0 / sr;
            const float velAmpScale = 1.0f;

            for (auto& v : voices)
            {
                if (!v.active) continue;
                const Patch& P = bank[v.patch];
                const float vlev = (0.2f + 0.8f * v.vel) * velAmpScale;

                // ------------------------------------------------ drums
                if (P.kit >= 0)
                {
                    for (int f = 0; f < frames; ++f)
                    {
                        if (v.t >= v.drumDur) { v.active = false; break; }
                        float s = DrumSample(v, P) * vlev * P.gain * v.gainMul;
                        v.t += dt;
                        out[f * 2] += s * v.panL * 1.4142f;
                        out[f * 2 + 1] += s * v.panR * 1.4142f;
                    }
                    continue;
                }

                // ------------------------------------------------ melodic
                const float aTime = std::max(0.0005f, P.a * v.atkMul);
                const float rTime = std::max(0.004f, P.r * v.relMul);
                const float attackInc = 1.0f / (aTime * sr);
                const float decK = 1.0f - std::exp(-4.0f / (std::max(0.01f, P.d) * sr));
                const float relK = 1.0f - std::exp(-4.0f / (rTime * sr));
                const float fdK = 1.0f - std::exp(-4.0f / (std::max(0.01f, P.fDecay) * sr));
                const float fmK = 1.0f - std::exp(-4.0f / (std::max(0.01f, P.fmDecay) * sr));
                const float peK = 1.0f - std::exp(-4.0f / (std::max(0.005f, P.pitchEnvDecay) * sr));
                const bool useFilter = P.cutoff < 19000.0f || P.envOct != 0.0f || P.fltLfoOct != 0.0f;
                const bool useFm = P.fmIndex > 0.0f;
                const float uniNorm = 1.0f / std::sqrt(static_cast<float>(P.uni));
                const int uni = std::clamp(P.uni, 1, 5);
                const float sustain = P.s;

                for (int f = 0; f < frames; ++f)
                {
                    // envelopes
                    if (v.stage == 0)
                    {
                        v.env += attackInc;
                        if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; }
                    }
                    else if (v.stage == 1) v.env += (sustain - v.env) * decK;
                    else
                    {
                        v.env -= v.env * relK;
                        if (v.env < 0.0004f) { v.active = false; break; }
                    }
                    v.fenv += (P.fSus - v.fenv) * fdK;
                    v.fmEnv -= v.fmEnv * fmK;
                    v.pEnv -= v.pEnv * peK;

                    // modulation
                    float modMul = 1.0f;
                    if (P.vibCents != 0.0f)
                    {
                        const float depth = P.vibDelay > 0.0f ? std::min(1.0f, static_cast<float>(v.t) / P.vibDelay) : 1.0f;
                        v.lfoPh += P.vibRate * dt; if (v.lfoPh >= 1.0) v.lfoPh -= 1.0;
                        modMul += 0.0005776f * P.vibCents * depth * static_cast<float>(std::sin(6.283185307179586 * v.lfoPh));
                    }
                    if (P.pitchEnv != 0.0f)
                        modMul *= std::exp2(P.pitchEnv * v.pEnv / 12.0f);

                    float pw = P.pw;
                    if (P.pwmDepth != 0.0f)
                    {
                        v.pwmPh += P.pwmRate * dt; if (v.pwmPh >= 1.0) v.pwmPh -= 1.0;
                        pw = std::clamp(P.pw + P.pwmDepth * static_cast<float>(std::sin(6.283185307179586 * v.pwmPh)) * 0.5f, 0.08f, 0.92f);
                    }

                    // oscillators
                    float s = 0.0f;
                    if (P.l1 > 0.0f)
                    {
                        float u = 0.0f;
                        float fmv = 0.0f;
                        if (useFm)
                        {
                            v.phMod += v.incMod * modMul; if (v.phMod >= 1.0) v.phMod -= 1.0;
                            fmv = P.fmIndex * v.fmEnv * static_cast<float>(std::sin(6.283185307179586 * v.phMod));
                        }
                        for (int i = 0; i < uni; ++i)
                        {
                            const double inc = v.incU[i] * modMul;
                            if (useFm)
                                u += static_cast<float>(std::sin(6.283185307179586 * v.ph[i] + fmv));
                            else
                                u += OscSample(P.w1, v.ph[i], inc, pw, v.rng);
                            v.ph[i] += inc; if (v.ph[i] >= 1.0) v.ph[i] -= 1.0;
                        }
                        s += u * uniNorm * P.l1;
                    }
                    if (P.l2 > 0.0f)
                    {
                        const double inc = v.inc2 * modMul;
                        s += OscSample(P.w2, v.ph2, inc, pw, v.rng) * P.l2;
                        v.ph2 += inc; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
                    }
                    if (P.sub > 0.0f)
                    {
                        s += static_cast<float>(std::sin(6.283185307179586 * v.phSub)) * P.sub;
                        v.phSub += v.incSub * modMul; if (v.phSub >= 1.0) v.phSub -= 1.0;
                    }
                    if (P.noise > 0.0f) s += NextNoise(v.rng) * P.noise;

                    // filter (coefficients every 8 samples)
                    if (useFilter)
                    {
                        if ((f & 7) == 0)
                        {
                            float oct = P.envOct * v.fenv + P.velOct * (v.vel - 0.5f) * 2.0f;
                            if (P.fltLfoOct != 0.0f)
                            {
                                v.fltLfoPh += P.fltLfoRate * dt * 8.0; if (v.fltLfoPh >= 1.0) v.fltLfoPh -= 1.0;
                                oct += P.fltLfoOct * static_cast<float>(std::sin(6.283185307179586 * v.fltLfoPh));
                            }
                            float fc = P.cutoff * v.cutMul * v.keyMul * std::exp2(oct);
                            fc = std::clamp(fc, 25.0f, std::min(18000.0f, sr * 0.45f));
                            v.g = static_cast<float>(std::tan(3.141592653589793 * fc / sr));
                            v.k = 2.0f - 1.95f * std::clamp(P.res, 0.0f, 1.0f);
                            v.a1 = 1.0f / (1.0f + v.g * (v.g + v.k));
                            v.a2 = v.g * v.a1;
                            v.a3 = v.g * v.a2;
                        }
                        const float v3 = s - v.ic2;
                        const float v1 = v.a1 * v.ic1 + v.a2 * v3;
                        const float v2 = v.ic2 + v.a2 * v.ic1 + v.a3 * v3;
                        v.ic1 = 2.0f * v1 - v.ic1;
                        v.ic2 = 2.0f * v2 - v.ic2;
                        s = v2;
                    }

                    if (P.drive > 0.0f)
                    {
                        const float x = s * (1.0f + P.drive * 3.0f);
                        s = x / (1.0f + std::fabs(x)) * (1.0f + P.drive * 0.8f);
                    }

                    if (P.tremDepth > 0.0f)
                    {
                        v.tremPh += P.tremRate * dt; if (v.tremPh >= 1.0) v.tremPh -= 1.0;
                        s *= 1.0f - P.tremDepth * 0.5f * (1.0f + static_cast<float>(std::sin(6.283185307179586 * v.tremPh)));
                    }

                    s *= v.env * vlev * P.gain * v.gainMul;
                    v.t += dt;
                    out[f * 2] += s * v.panL * 1.4142f;
                    out[f * 2 + 1] += s * v.panR * 1.4142f;
                }
            }
        }
    };
}