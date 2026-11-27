#pragma once
// EasyMix Studio: MIDI note data / Piano Roll editor (ImGui) / built-in fallback synth
//
//  Piano Roll features
//   - Tools: Draw (D) / Select (S) / Erase (E)
//   - Multi selection (Ctrl/Shift click, box select), group move / resize
//   - Ctrl+A / Ctrl+C / Ctrl+V / Ctrl+D / Delete / arrow keys
//   - Quantize, Legato, Humanize, Transpose, Octave shift (selection or whole track)
//   - Scale highlight + "lock to scale" (11 scales, 12 roots)
//   - Velocity lane (drag the bar of a note), velocity colored notes
//   - Ghost notes of the other tracks, key highlight for selected / sounding notes
//   - Ruler with bar numbers (click / drag = move playhead), Ctrl + mouse wheel zooms horizontally
//   - Audition: sound on draw / key click / move / arrow transpose, chord drawing, follow playhead
//   - Alt+drag copies notes, drag in empty draw area sets length, Q / [ / ] / Esc shortcuts
//   - Transform / Velocity / Select menus (reverse, invert, stretch, strum, arpeggio, swing, join ...)
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

struct MidiNote
{
    int id = 0;
    int trackIndex = 0;
    int pitch = 60;
    float startBeat = 0.0f;
    float lengthBeats = 1.0f;
    float velocity = 0.8f;
};

struct PianoRollOrig
{
    int id = 0;
    int pitch = 60;
    float start = 0.0f;
    float len = 1.0f;
};

struct PianoRollState
{
    float pixelsPerBeat = 64.0f;
    float noteHeight = 14.0f;
    int selectedId = -1;       // "primary" selected note (-1 = nothing selected)
    int dragId = -1;
    int dragMode = 0;          // 1 = move, 2 = resize, 3 = box select, 5 = erase drag
    float dragGrabBeat = 0.0f;
    int nextId = 1;            // after loading a project: set to (max id + 1)
    float lastLength = 1.0f;
    bool scrolledInit = false;

    // ---- editor state ----
    int tool = 0;              // 0 = draw, 1 = select, 2 = erase
    std::vector<int> sel;      // all selected note ids
    std::vector<PianoRollOrig> orig;
    int dragStartPitch = 60;
    ImVec2 boxStart = ImVec2(0, 0);   // canvas local coordinates
    std::vector<MidiNote> clipboard;

    int scaleRoot = 0;
    int scaleType = 0;         // 0 = chromatic (no highlight)
    bool showScale = true;
    bool lockScale = false;
    bool ghostNotes = true;
    bool showVelocity = true;
    float velLaneH = 110.0f;
    float scrollX = 0.0f;
    int velDragId = -1;
    float defaultVel = 0.8f;
    float humanizeAmount = 1.0f;

    // ---- usability additions ----
    std::function<void(int, float, bool)> audition;   // (pitch, velocity, additive) set by the host
    float seekBeat = -1.0f;      // >= 0 : host should move the playhead here (then reset to -1)
    bool follow = true;
    bool auditionOn = true;
    bool drawExtend = true;      // drag after drawing a note sets its length (false: moves the note)
    bool showNames = true;
    int chordType = 0;
    int chordInv = 0;
    float swing = 0.25f;
    float pressX = 0.0f;
    bool rulerDrag = false;
    bool keyHeld = false;
    int lastKeyPitch = -1;
    int lastAudPitch = -1;
    bool velPaint = false;
    char hoverText[48] = "";
};

inline const char* MidiPitchName(int pitch, char* buf, size_t n)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    std::snprintf(buf, n, "%s%d", names[pitch % 12], pitch / 12 - 1);
    return buf;
}

namespace prdetail
{
    struct ScaleDef { const char* name; int count; int iv[8]; };

    inline const ScaleDef* Scales(int& n)
    {
        static const ScaleDef defs[] =
        {
            { "Chromatic",        12, { 0,1,2,3,4,5,6,7 } },
            { "Major",             7, { 0,2,4,5,7,9,11,0 } },
            { "Minor",             7, { 0,2,3,5,7,8,10,0 } },
            { "Dorian",            7, { 0,2,3,5,7,9,10,0 } },
            { "Phrygian",          7, { 0,1,3,5,7,8,10,0 } },
            { "Lydian",            7, { 0,2,4,6,7,9,11,0 } },
            { "Mixolydian",        7, { 0,2,4,5,7,9,10,0 } },
            { "Harmonic Minor",    7, { 0,2,3,5,7,8,11,0 } },
            { "Pentatonic Major",  5, { 0,2,4,7,9,0,0,0 } },
            { "Pentatonic Minor",  5, { 0,3,5,7,10,0,0,0 } },
            { "Blues",             6, { 0,3,5,6,7,10,0,0 } },
        };
        n = static_cast<int>(sizeof(defs) / sizeof(defs[0]));
        return defs;
    }

    inline bool InScale(int pitch, int root, int type)
    {
        int n = 0;
        const ScaleDef* d = Scales(n);
        type = std::clamp(type, 0, n - 1);
        if (type == 0) return true;
        const int rel = ((pitch - root) % 12 + 12) % 12;
        for (int i = 0; i < d[type].count && i < 8; ++i)
            if (d[type].iv[i] == rel) return true;
        return false;
    }

    inline int NearestInScale(int pitch, int root, int type)
    {
        for (int d = 0; d <= 6; ++d)
        {
            if (pitch + d <= 127 && InScale(pitch + d, root, type)) return pitch + d;
            if (pitch - d >= 0 && InScale(pitch - d, root, type)) return pitch - d;
        }
        return pitch;
    }

    inline int ScaleStep(int pitch, int steps, int root, int type)
    {
        const int ty = type == 0 ? 1 : type;
        int p = NearestInScale(pitch, root, ty);
        int count = 0;
        while (count < steps && p < 127)
        {
            ++p;
            if (InScale(p, root, ty)) ++count;
        }
        return p;
    }

    inline const char* const* ChordNames(int& n)
    {
        static const char* names[] = { "Off", "Major", "Minor", "Dim", "Aug", "Sus2", "Sus4", "Power (5th)", "Octave",
            "6", "m6", "Maj7", "Min7", "Dom7", "Min7b5", "Dim7", "Maj9", "Min9", "Add9", "Dom9",
            "Diatonic triad", "Diatonic 7th" };
        n = static_cast<int>(sizeof(names) / sizeof(names[0]));
        return names;
    }

    // returns the chord pitches, the clicked (root) note first
    inline std::vector<int> ChordPitches(int root, int type, int inv, int scaleRoot, int scaleType)
    {
        static const int tbl[][6] =
        {
            { 0, 0, 0, 0, 0, 0 },          // Off
            { 3, 0, 4, 7, 0, 0 },          // Major
            { 3, 0, 3, 7, 0, 0 },          // Minor
            { 3, 0, 3, 6, 0, 0 },          // Dim
            { 3, 0, 4, 8, 0, 0 },          // Aug
            { 3, 0, 2, 7, 0, 0 },          // Sus2
            { 3, 0, 5, 7, 0, 0 },          // Sus4
            { 3, 0, 7, 12, 0, 0 },         // Power
            { 2, 0, 12, 0, 0, 0 },         // Octave
            { 4, 0, 4, 7, 9, 0 },          // 6
            { 4, 0, 3, 7, 9, 0 },          // m6
            { 4, 0, 4, 7, 11, 0 },         // Maj7
            { 4, 0, 3, 7, 10, 0 },         // Min7
            { 4, 0, 4, 7, 10, 0 },         // Dom7
            { 4, 0, 3, 6, 10, 0 },         // Min7b5
            { 4, 0, 3, 6, 9, 0 },          // Dim7
            { 5, 0, 4, 7, 11, 14 },        // Maj9
            { 5, 0, 3, 7, 10, 14 },        // Min9
            { 4, 0, 4, 7, 14, 0 },         // Add9
            { 5, 0, 4, 7, 10, 14 },        // Dom9
        };
        std::vector<int> out;
        out.push_back(root);
        if (type <= 0) return out;
        if (type >= 20)
        {
            out.push_back(ScaleStep(root, 2, scaleRoot, scaleType));
            out.push_back(ScaleStep(root, 4, scaleRoot, scaleType));
            if (type == 21) out.push_back(ScaleStep(root, 6, scaleRoot, scaleType));
        }
        else
        {
            const int cnt = tbl[type][0];   // notes including the root; offsets are tbl[type][1..cnt]
            for (int i = 2; i <= cnt; ++i) out.push_back(root + tbl[type][i]);
        }
        // inversion: raise the lowest notes by an octave (the clicked note stays first in the list)
        std::vector<int> idx(out.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<int>(i);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return out[a] < out[b]; });
        for (int k = 0; k < inv && k < static_cast<int>(idx.size()) - 1; ++k) out[idx[k]] += 12;
        std::vector<int> res;
        for (int p : out) if (p >= 0 && p <= 127) res.push_back(p);
        if (res.empty()) res.push_back(std::clamp(root, 0, 127));
        return res;
    }

    inline ImU32 VelocityColor(float v, bool ghost = false)
    {
        v = std::clamp(v, 0.0f, 1.0f);
        const int r = 55 + static_cast<int>(v * 190.0f);
        const int g = 150 - static_cast<int>(v * 60.0f);
        const int b = 215 - static_cast<int>(v * 150.0f);
        return ghost ? IM_COL32(r, g, b, 70) : IM_COL32(r, g, b, 255);
    }
}

// Draw the contents of an ImGui window (call between Begin/End).
inline void DrawPianoRoll(
    PianoRollState& st,
    std::vector<MidiNote>& notes,
    int track,
    float totalBeats,
    bool snapEnabled,
    float snapBeat,
    float playheadBeat,
    bool isPlaying = false)
{
    using namespace prdetail;
    ImGuiIO& io = ImGui::GetIO();

    // ---------------------------------------------------------------- selection upkeep
    if (st.selectedId == -1)
        st.sel.clear();
    st.sel.erase(std::remove_if(st.sel.begin(), st.sel.end(), [&](int id)
        {
            return std::none_of(notes.begin(), notes.end(), [&](const MidiNote& n)
                { return n.id == id && n.trackIndex == track; });
        }), st.sel.end());
    if (st.sel.empty())
        st.selectedId = -1;
    else if (std::find(st.sel.begin(), st.sel.end(), st.selectedId) == st.sel.end())
        st.selectedId = st.sel.back();

    auto isSel = [&](int id)
        {
            return std::find(st.sel.begin(), st.sel.end(), id) != st.sel.end();
        };
    auto selectOnly = [&](int id)
        {
            st.sel.clear();
            st.sel.push_back(id);
            st.selectedId = id;
        };
    auto removeFromSel = [&](int id)
        {
            st.sel.erase(std::remove(st.sel.begin(), st.sel.end(), id), st.sel.end());
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };
    auto snapB = [&](float b)
        {
            return (snapEnabled && snapBeat > 0.0f) ? std::round(b / snapBeat) * snapBeat : b;
        };
    const float minLen = (snapEnabled && snapBeat > 0.0f) ? snapBeat : 0.0625f;

    // apply fn to the selected notes (or to every note of the track when nothing is selected)
    auto forTarget = [&](auto fn)
        {
            const bool any = !st.sel.empty();
            for (auto& n : notes)
            {
                if (n.trackIndex != track) continue;
                if (any && !isSel(n.id)) continue;
                fn(n);
            }
        };

    // ---------------------------------------------------------------- edit operations
    auto opSelectAll = [&]()
        {
            st.sel.clear();
            for (const auto& n : notes)
                if (n.trackIndex == track) st.sel.push_back(n.id);
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };
    auto opDelete = [&]()
        {
            if (st.sel.empty()) return;
            const std::vector<int> ids = st.sel;
            notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const MidiNote& n)
                {
                    return n.trackIndex == track &&
                        std::find(ids.begin(), ids.end(), n.id) != ids.end();
                }), notes.end());
            st.sel.clear();
            st.selectedId = -1;
            st.dragId = -1;
        };
    auto opCopy = [&]()
        {
            st.clipboard.clear();
            for (const auto& n : notes)
                if (n.trackIndex == track && isSel(n.id)) st.clipboard.push_back(n);
        };
    auto opPaste = [&]()
        {
            if (st.clipboard.empty()) return;
            float minS = st.clipboard.front().startBeat;
            for (const auto& n : st.clipboard) minS = std::min(minS, n.startBeat);
            const float base = std::max(0.0f, snapB(playheadBeat));
            std::vector<MidiNote> added;
            for (const auto& c : st.clipboard)
            {
                MidiNote n = c;
                n.id = st.nextId++;
                n.trackIndex = track;
                n.startBeat = base + (c.startBeat - minS);
                added.push_back(n);
            }
            st.sel.clear();
            for (const auto& n : added)
            {
                notes.push_back(n);
                st.sel.push_back(n.id);
            }
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };
    auto opDuplicate = [&]()
        {
            if (st.sel.empty()) return;
            float minS = 1.0e9f, maxE = 0.0f;
            for (const auto& n : notes)
            {
                if (n.trackIndex != track || !isSel(n.id)) continue;
                minS = std::min(minS, n.startBeat);
                maxE = std::max(maxE, n.startBeat + n.lengthBeats);
            }
            const float span = std::max(minLen, maxE - minS);
            std::vector<MidiNote> added;
            for (const auto& n : notes)
            {
                if (n.trackIndex != track || !isSel(n.id)) continue;
                MidiNote c = n;
                c.id = st.nextId++;
                c.startBeat = n.startBeat + span;
                added.push_back(c);
            }
            st.sel.clear();
            for (const auto& c : added)
            {
                notes.push_back(c);
                st.sel.push_back(c.id);
            }
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };
    auto opQuantize = [&]()
        {
            forTarget([&](MidiNote& n)
                {
                    n.startBeat = std::max(0.0f, snapB(n.startBeat));
                    n.lengthBeats = std::max(minLen, snapB(n.lengthBeats));
                });
        };
    auto opLegato = [&]()
        {
            std::vector<MidiNote*> v;
            forTarget([&](MidiNote& n) { v.push_back(&n); });
            std::sort(v.begin(), v.end(), [](const MidiNote* a, const MidiNote* b)
                { return a->startBeat < b->startBeat; });
            for (size_t i = 0; i < v.size(); ++i)
            {
                for (size_t j = i + 1; j < v.size(); ++j)
                {
                    if (v[j]->startBeat > v[i]->startBeat + 0.0001f)
                    {
                        v[i]->lengthBeats = std::max(0.0625f, v[j]->startBeat - v[i]->startBeat);
                        break;
                    }
                }
            }
        };
    auto opHumanize = [&]()
        {
            const float amt = std::clamp(st.humanizeAmount, 0.0f, 4.0f);
            forTarget([&](MidiNote& n)
                {
                    const float r1 = (static_cast<float>(std::rand() % 2001) / 1000.0f) - 1.0f;
                    const float r2 = (static_cast<float>(std::rand() % 2001) / 1000.0f) - 1.0f;
                    n.startBeat = std::max(0.0f, n.startBeat + r1 * 0.03f * amt);
                    n.velocity = std::clamp(n.velocity + r2 * 0.10f * amt, 0.05f, 1.0f);
                });
        };
    auto opTranspose = [&](int semis)
        {
            forTarget([&](MidiNote& n) { n.pitch = std::clamp(n.pitch + semis, 0, 127); });
        };

    auto auditionPrimary = [&]()
        {
            if (!st.audition || !st.auditionOn) return;
            for (const auto& n : notes)
                if (n.id == st.selectedId && n.trackIndex == track) { st.audition(n.pitch, n.velocity, false); break; }
        };
    auto sortedTargets = [&]()
        {
            std::vector<MidiNote*> v;
            forTarget([&](MidiNote& n) { v.push_back(&n); });
            std::sort(v.begin(), v.end(), [](const MidiNote* a, const MidiNote* b)
                {
                    if (std::fabs(a->startBeat - b->startBeat) > 0.0005f) return a->startBeat < b->startBeat;
                    return a->pitch < b->pitch;
                });
            return v;
        };
    auto removeIds = [&](const std::vector<int>& ids)
        {
            notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const MidiNote& n)
                { return n.trackIndex == track && std::find(ids.begin(), ids.end(), n.id) != ids.end(); }), notes.end());
        };
    auto opReverse = [&]()
        {
            float minS = 1.0e9f, maxE = 0.0f;
            forTarget([&](MidiNote& n) { minS = std::min(minS, n.startBeat); maxE = std::max(maxE, n.startBeat + n.lengthBeats); });
            if (maxE <= minS) return;
            forTarget([&](MidiNote& n) { n.startBeat = minS + maxE - (n.startBeat + n.lengthBeats); });
        };
    auto opInvertPitch = [&]()
        {
            int lo = 127, hi = 0;
            forTarget([&](MidiNote& n) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); });
            forTarget([&](MidiNote& n) { n.pitch = std::clamp(lo + hi - n.pitch, 0, 127); });
        };
    auto opStretch = [&](float k)
        {
            float minS = 1.0e9f;
            forTarget([&](MidiNote& n) { minS = std::min(minS, n.startBeat); });
            forTarget([&](MidiNote& n)
                {
                    n.startBeat = minS + (n.startBeat - minS) * k;
                    n.lengthBeats = std::max(0.0625f, n.lengthBeats * k);
                });
        };
    auto opSplitHalf = [&]()
        {
            std::vector<MidiNote> added;
            forTarget([&](MidiNote& n)
                {
                    if (n.lengthBeats < 0.125f) return;
                    n.lengthBeats *= 0.5f;
                    MidiNote c = n;
                    c.id = st.nextId++;
                    c.startBeat = n.startBeat + n.lengthBeats;
                    added.push_back(c);
                });
            for (const auto& c : added) { notes.push_back(c); if (!st.sel.empty()) st.sel.push_back(c.id); }
        };
    auto opJoin = [&]()
        {
            std::vector<MidiNote*> v;
            forTarget([&](MidiNote& n) { v.push_back(&n); });
            std::sort(v.begin(), v.end(), [](const MidiNote* a, const MidiNote* b)
                {
                    if (a->pitch != b->pitch) return a->pitch < b->pitch;
                    return a->startBeat < b->startBeat;
                });
            std::vector<int> kill;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (std::find(kill.begin(), kill.end(), v[i]->id) != kill.end()) continue;
                for (size_t j = i + 1; j < v.size(); ++j)
                {
                    if (v[j]->pitch != v[i]->pitch) break;
                    if (v[j]->startBeat <= v[i]->startBeat + v[i]->lengthBeats + 0.001f)
                    {
                        const float e = std::max(v[i]->startBeat + v[i]->lengthBeats, v[j]->startBeat + v[j]->lengthBeats);
                        v[i]->lengthBeats = e - v[i]->startBeat;
                        kill.push_back(v[j]->id);
                    }
                    else break;
                }
            }
            removeIds(kill);
            for (int id : kill) removeFromSel(id);
        };
    auto opFitScale = [&]()
        {
            if (st.scaleType == 0) return;
            forTarget([&](MidiNote& n) { n.pitch = NearestInScale(n.pitch, st.scaleRoot, st.scaleType); });
        };
    auto opVelRamp = [&](float a, float b)
        {
            float minS = 1.0e9f, maxS = 0.0f;
            forTarget([&](MidiNote& n) { minS = std::min(minS, n.startBeat); maxS = std::max(maxS, n.startBeat); });
            const float span = std::max(0.001f, maxS - minS);
            forTarget([&](MidiNote& n)
                { n.velocity = std::clamp(a + (b - a) * (n.startBeat - minS) / span, 0.01f, 1.0f); });
        };
    auto opRandVel = [&]()
        {
            forTarget([&](MidiNote& n)
                { n.velocity = 0.45f + 0.55f * (static_cast<float>(std::rand() % 1001) / 1000.0f); });
        };
    auto opSetVel = [&]() { forTarget([&](MidiNote& n) { n.velocity = st.defaultVel; }); };
    auto opSetLen = [&]() { forTarget([&](MidiNote& n) { n.lengthBeats = std::max(minLen, st.lastLength); }); };
    auto opLenDelta = [&](int dir)
        {
            const float step = (snapEnabled && snapBeat > 0.0f) ? snapBeat : 0.25f;
            forTarget([&](MidiNote& n) { n.lengthBeats = std::max(minLen, n.lengthBeats + dir * step); });
        };
    auto opStrum = [&]()
        {
            auto v = sortedTargets();
            size_t i = 0;
            while (i < v.size())
            {
                size_t j = i;
                const float gs = v[i]->startBeat;
                while (j < v.size() && std::fabs(v[j]->startBeat - gs) < 0.001f) ++j;
                for (size_t k = i; k < j; ++k) v[k]->startBeat = gs + static_cast<float>(k - i) * 0.0625f;
                i = j;
            }
        };
    auto opArp = [&](int dir)   // 1 up, 2 down, 3 up-down
        {
            auto v = sortedTargets();
            const float step = (snapEnabled && snapBeat > 0.0f) ? snapBeat : 0.25f;
            std::vector<MidiNote> added;
            std::vector<int> kill;
            size_t i = 0;
            while (i < v.size())
            {
                size_t j = i;
                const float gs = v[i]->startBeat;
                while (j < v.size() && std::fabs(v[j]->startBeat - gs) < 0.001f) ++j;
                if (j - i >= 2)
                {
                    std::vector<int> p;
                    float maxLen = 0.0f, velSum = 0.0f;
                    for (size_t k = i; k < j; ++k)
                    {
                        p.push_back(v[k]->pitch);
                        maxLen = std::max(maxLen, v[k]->lengthBeats);
                        velSum += v[k]->velocity;
                        kill.push_back(v[k]->id);
                    }
                    std::sort(p.begin(), p.end());
                    std::vector<int> seq;
                    if (dir == 2) seq.assign(p.rbegin(), p.rend());
                    else
                    {
                        seq = p;
                        if (dir == 3) for (int k = static_cast<int>(p.size()) - 2; k >= 1; --k) seq.push_back(p[k]);
                    }
                    const int count = std::max(1, static_cast<int>(maxLen / step + 0.001f));
                    for (int k = 0; k < count; ++k)
                    {
                        MidiNote c;
                        c.id = st.nextId++;
                        c.trackIndex = track;
                        c.pitch = seq[static_cast<size_t>(k) % seq.size()];
                        c.startBeat = gs + k * step;
                        c.lengthBeats = step;
                        c.velocity = velSum / static_cast<float>(j - i);
                        added.push_back(c);
                    }
                }
                i = j;
            }
            if (kill.empty()) return;
            removeIds(kill);
            st.sel.clear();
            for (const auto& c : added) { notes.push_back(c); st.sel.push_back(c.id); }
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };
    auto opSwing = [&]()
        {
            const float step = (snapEnabled && snapBeat > 0.0f) ? snapBeat : 0.25f;
            forTarget([&](MidiNote& n)
                {
                    const float q = n.startBeat / step;
                    const int qi = static_cast<int>(std::lround(q));
                    if ((qi & 1) && std::fabs(q - qi) < 0.2f + st.swing * 0.5f)
                        n.startBeat = std::max(0.0f, qi * step + st.swing * step);
                });
        };
    auto opSelectSame = [&]()
        {
            int pitch = -1;
            for (const auto& n : notes)
                if (n.id == st.selectedId && n.trackIndex == track) pitch = n.pitch;
            if (pitch < 0) return;
            st.sel.clear();
            for (const auto& n : notes)
                if (n.trackIndex == track && n.pitch == pitch) st.sel.push_back(n.id);
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };
    auto opSelectAfter = [&]()
        {
            st.sel.clear();
            for (const auto& n : notes)
                if (n.trackIndex == track && n.startBeat >= playheadBeat - 0.001f) st.sel.push_back(n.id);
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };
    auto opInvertSel = [&]()
        {
            std::vector<int> nsel;
            for (const auto& n : notes)
                if (n.trackIndex == track && !isSel(n.id)) nsel.push_back(n.id);
            st.sel = nsel;
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
        };

    // ---------------------------------------------------------------- toolbar
    auto toolBtn = [&](const char* label, int t, const char* tip)
        {
            const bool a = st.tool == t;
            if (a) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.52f, 0.12f, 1.0f));
            if (ImGui::Button(label)) st.tool = t;
            if (a) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        };

    toolBtn("Draw", 0, "Draw (D)\nClick: add / Drag: move / Right edge: resize / Right click: delete");
    ImGui::SameLine();
    toolBtn("Select", 1, "Select (S)\nClick or box select / Ctrl,Shift: add to selection");
    ImGui::SameLine();
    toolBtn("Erase", 2, "Erase (E)\nClick or drag over notes to delete them");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderFloat("Zoom X", &st.pixelsPerBeat, 20.0f, 240.0f, "%.0f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderFloat("Zoom Y", &st.noteHeight, 8.0f, 28.0f, "%.0f");
    ImGui::SameLine();
    ImGui::Checkbox("Velocity", &st.showVelocity);
    ImGui::SameLine();
    ImGui::Checkbox("Ghost", &st.ghostNotes);

    // scale
    {
        static const char* roots[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        int sn = 0;
        const ScaleDef* defs = Scales(sn);
        static const char* scaleNames[16] = {};
        for (int i = 0; i < sn && i < 16; ++i) scaleNames[i] = defs[i].name;

        ImGui::SetNextItemWidth(56.0f);
        ImGui::Combo("Key", &st.scaleRoot, roots, 12);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::Combo("Scale", &st.scaleType, scaleNames, sn);
        ImGui::SameLine();
        ImGui::Checkbox("Show", &st.showScale);
        ImGui::SameLine();
        ImGui::Checkbox("Lock to scale", &st.lockScale);
    }

    // operations
    {
        if (ImGui::Button("Select All")) opSelectAll();
        ImGui::SameLine();
        if (ImGui::Button("Copy")) opCopy();
        ImGui::SameLine();
        if (ImGui::Button("Paste")) opPaste();
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) opDuplicate();
        ImGui::SameLine();
        if (ImGui::Button("Delete")) opDelete();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Quantize")) opQuantize();
        ImGui::SameLine();
        if (ImGui::Button("Legato")) opLegato();
        ImGui::SameLine();
        if (ImGui::Button("Humanize")) opHumanize();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragFloat("##hum", &st.humanizeAmount, 0.02f, 0.0f, 4.0f, "x%.2f");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("-1")) opTranspose(-1);
        ImGui::SameLine();
        if (ImGui::Button("+1")) opTranspose(1);
        ImGui::SameLine();
        if (ImGui::Button("-Oct")) opTranspose(-12);
        ImGui::SameLine();
        if (ImGui::Button("+Oct")) opTranspose(12);
        ImGui::SameLine();
        if (ImGui::Button("Clear All"))
        {
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                [&](const MidiNote& n) { return n.trackIndex == track; }), notes.end());
            st.sel.clear();
            st.selectedId = -1;
            st.dragId = -1;
        }
    }

    // chord / length / menus
    {
        static const float lenVals[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        static const char* lenNames[] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar" };
        const char* lenPrev = "custom";
        for (int i = 0; i < 6; ++i)
            if (std::fabs(st.lastLength - lenVals[i]) < 0.001f) lenPrev = lenNames[i];
        ImGui::SetNextItemWidth(74.0f);
        if (ImGui::BeginCombo("New len", lenPrev))
        {
            for (int i = 0; i < 6; ++i)
                if (ImGui::Selectable(lenNames[i], std::fabs(st.lastLength - lenVals[i]) < 0.001f)) st.lastLength = lenVals[i];
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        {
            int cn = 0;
            const char* const* cnames = ChordNames(cn);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("Chord", &st.chordType, cnames, cn);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(54.0f);
            ImGui::SliderInt("Inv", &st.chordInv, 0, 3);
        }
        ImGui::SameLine();
        if (ImGui::Button("Transform")) ImGui::OpenPopup("prTransform");
        if (ImGui::BeginPopup("prTransform"))
        {
            if (ImGui::MenuItem("Reverse")) opReverse();
            if (ImGui::MenuItem("Invert pitch")) opInvertPitch();
            if (ImGui::MenuItem("Stretch x2")) opStretch(2.0f);
            if (ImGui::MenuItem("Stretch x0.5")) opStretch(0.5f);
            if (ImGui::MenuItem("Split in half")) opSplitHalf();
            if (ImGui::MenuItem("Join notes")) opJoin();
            if (ImGui::MenuItem("Fit to scale")) opFitScale();
            ImGui::Separator();
            if (ImGui::MenuItem("Strum chord")) opStrum();
            if (ImGui::MenuItem("Arpeggiate (up)")) opArp(1);
            if (ImGui::MenuItem("Arpeggiate (down)")) opArp(2);
            if (ImGui::MenuItem("Arpeggiate (up-down)")) opArp(3);
            ImGui::Separator();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderFloat("##swing", &st.swing, 0.0f, 0.5f, "Swing %.2f");
            if (ImGui::MenuItem("Apply swing")) opSwing();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Vel / Len")) ImGui::OpenPopup("prVel");
        if (ImGui::BeginPopup("prVel"))
        {
            if (ImGui::MenuItem("Crescendo")) opVelRamp(0.3f, 1.0f);
            if (ImGui::MenuItem("Decrescendo")) opVelRamp(1.0f, 0.3f);
            if (ImGui::MenuItem("Randomize velocity")) opRandVel();
            if (ImGui::MenuItem("Set velocity = new-note velocity")) opSetVel();
            ImGui::Separator();
            if (ImGui::MenuItem("Set length = new-note length")) opSetLen();
            if (ImGui::MenuItem("Longer  (])")) opLenDelta(1);
            if (ImGui::MenuItem("Shorter ([)")) opLenDelta(-1);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Select")) ImGui::OpenPopup("prSelect");
        if (ImGui::BeginPopup("prSelect"))
        {
            if (ImGui::MenuItem("Same pitch as selected")) opSelectSame();
            if (ImGui::MenuItem("From playhead onward")) opSelectAfter();
            if (ImGui::MenuItem("Invert selection")) opInvertSel();
            if (ImGui::MenuItem("Deselect (Esc)")) { st.sel.clear(); st.selectedId = -1; }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Options")) ImGui::OpenPopup("prOptions");
        if (ImGui::BeginPopup("prOptions"))
        {
            ImGui::Checkbox("Follow playhead", &st.follow);
            ImGui::Checkbox("Audition notes", &st.auditionOn);
            ImGui::Checkbox("Draw: drag sets length", &st.drawExtend);
            ImGui::Checkbox("Show note names", &st.showNames);
            ImGui::TextDisabled("Alt+drag: copy notes   Ruler click: move playhead");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", st.hoverText);
    }

    // selected note controls
    {
        auto primary = std::find_if(notes.begin(), notes.end(), [&](const MidiNote& n)
            { return n.id == st.selectedId && n.trackIndex == track; });

        if (primary != notes.end())
        {
            char nm[8];
            if (st.sel.size() == 1)
                ImGui::Text("Note: %s", MidiPitchName(primary->pitch, nm, sizeof(nm)));
            else
                ImGui::Text("%d notes", static_cast<int>(st.sel.size()));
            ImGui::SameLine();
            float v = primary->velocity;
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::SliderFloat("Velocity##sel", &v, 0.01f, 1.0f))
                for (auto& n : notes)
                    if (n.trackIndex == track && isSel(n.id)) n.velocity = v;
            ImGui::SameLine();
            float len = primary->lengthBeats;
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Length(beat)", &len, 0.05f, 0.0625f, 64.0f))
                for (auto& n : notes)
                    if (n.trackIndex == track && isSel(n.id)) n.lengthBeats = std::max(0.0625f, len);
        }
        else
        {
            ImGui::SetNextItemWidth(130.0f);
            ImGui::SliderFloat("New note velocity", &st.defaultVel, 0.01f, 1.0f);
            ImGui::SameLine();
            ImGui::TextDisabled("Ctrl+A/C/V/D  Del  Arrows  Q  [ ]  Alt+drag copy  Ctrl+Wheel zoom  (D/S/E = tools)");
        }
    }

    // ---------------------------------------------------------------- canvas
    const float keyW = 64.0f;
    const float rulerH = 20.0f;
    const float nh = st.noteHeight;
    const float ppb = st.pixelsPerBeat;

    float canvasH = 0.0f;
    if (st.showVelocity)
        canvasH = std::max(120.0f, ImGui::GetContentRegionAvail().y - st.velLaneH - ImGui::GetStyle().ItemSpacing.y);

    ImGui::BeginChild("PianoRollCanvas", ImVec2(0, canvasH), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_HorizontalScrollbar);

    const float W = keyW + totalBeats * ppb;
    const float H = 128.0f * nh;

    if (!st.scrolledInit)
    {
        ImGui::SetScrollY((127 - 84) * nh);   // around C6
        st.scrolledInit = true;
    }

    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##pianoroll", ImVec2(W, H),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 m = io.MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto beatToX = [&](float b) { return o.x + keyW + b * ppb; };
    auto xToBeat = [&](float x) { return (x - o.x - keyW) / ppb; };
    auto pitchToY = [&](int p) { return o.y + (127 - p) * nh; };
    auto yToPitch = [&](float y)
        {
            int p = 127 - static_cast<int>(std::floor((y - o.y) / nh));
            return std::clamp(p, 0, 127);
        };
    auto hitNote = [&]() -> int
        {
            for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
            {
                const MidiNote& n = notes[i];
                if (n.trackIndex != track) continue;
                const float x1 = beatToX(n.startBeat);
                const float x2 = beatToX(n.startBeat + n.lengthBeats);
                const float y1 = pitchToY(n.pitch);
                if (m.x >= x1 && m.x <= x2 && m.y >= y1 && m.y < y1 + nh) return i;
            }
            return -1;
        };
    auto findNote = [&](int id) -> MidiNote*
        {
            for (auto& n : notes)
                if (n.id == id && n.trackIndex == track) return &n;
            return nullptr;
        };
    auto eraseNoteAt = [&](int idx)
        {
            const int id = notes[idx].id;
            notes.erase(notes.begin() + idx);
            removeFromSel(id);
            if (st.dragId == id) st.dragId = -1;
        };

    const float keyX = o.x + ImGui::GetScrollX();
    const float rulerTop = o.y + ImGui::GetScrollY();
    const float gridTop = rulerTop + rulerH;

    // Ctrl + wheel: horizontal zoom
    if (hovered && io.KeyCtrl && io.MouseWheel != 0.0f)
        st.pixelsPerBeat = std::clamp(st.pixelsPerBeat * (1.0f + 0.1f * io.MouseWheel), 20.0f, 240.0f);

    const bool inGrid = hovered && m.x >= keyX + keyW && m.y >= gridTop;
    const bool additive = io.KeyCtrl || io.KeyShift;

    // follow playhead while playing
    if (st.follow && isPlaying)
    {
        const float px = playheadBeat * ppb;
        const float sx = ImGui::GetScrollX();
        const float vw = ImGui::GetWindowWidth() - keyW;
        if (px < sx || px > sx + vw - 80.0f)
            ImGui::SetScrollX(std::max(0.0f, px - 60.0f));
    }

    // hover info (pitch + bar:beat:percent) shown in the toolbar next frame
    if (inGrid)
    {
        char nm[8];
        MidiPitchName(yToPitch(m.y), nm, sizeof(nm));
        const float hb = std::max(0.0f, xToBeat(m.x));
        std::snprintf(st.hoverText, sizeof(st.hoverText), "%s  %d:%d:%02d", nm,
            static_cast<int>(hb / 4.0f) + 1, static_cast<int>(std::fmod(hb, 4.0f)) + 1,
            static_cast<int>(std::fmod(hb, 1.0f) * 100.0f));
    }

    // ruler: click / drag moves the playhead
    {
        const bool inRuler = hovered && m.x >= keyX + keyW && m.y >= rulerTop && m.y < gridTop;
        if (inRuler && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st.rulerDrag = true;
        if (st.rulerDrag)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) st.seekBeat = std::max(0.0f, snapB(xToBeat(m.x)));
            else st.rulerDrag = false;
        }
    }

    // piano keys: click / glissando to audition
    {
        const bool inKeys = hovered && m.x < keyX + keyW && m.y >= gridTop;
        if (inKeys && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { st.keyHeld = true; st.lastKeyPitch = -1; }
        if (st.keyHeld)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (inKeys)
                {
                    const int kp = yToPitch(m.y);
                    if (kp != st.lastKeyPitch)
                    {
                        st.lastKeyPitch = kp;
                        if (st.audition) st.audition(kp, st.defaultVel, false);
                    }
                }
            }
            else st.keyHeld = false;
        }
    }

    // resize cursor on the right edge of a note
    if (inGrid && st.dragId == -1 && st.dragMode == 0 && st.tool != 2)
    {
        const int hh = hitNote();
        if (hh >= 0)
        {
            const float x1 = beatToX(notes[hh].startBeat);
            const float x2 = beatToX(notes[hh].startBeat + notes[hh].lengthBeats);
            if ((x2 - x1) > 12.0f && m.x > x2 - 6.0f) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
    }

    // ---- mouse: press ----
    if (inGrid && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const int hi = hitNote();

        if (st.tool == 2)
        {
            if (hi >= 0) eraseNoteAt(hi);
            st.dragMode = 5;
            st.dragId = -1;
        }
        else if (hi >= 0)
        {
            const int id = notes[hi].id;
            bool keep = true;
            int dragTarget = id;
            st.lastAudPitch = -1;

            if (additive)
            {
                if (isSel(id)) { removeFromSel(id); keep = false; }
                else { st.sel.push_back(id); st.selectedId = id; }
            }
            else if (!isSel(id))
            {
                selectOnly(id);
            }

            if (keep && io.KeyAlt)
            {
                std::vector<MidiNote> added;
                for (const auto& nn : notes)
                {
                    if (nn.trackIndex != track || !isSel(nn.id)) continue;
                    MidiNote c = nn;
                    c.id = st.nextId++;
                    if (nn.id == id) dragTarget = c.id;
                    added.push_back(c);
                }
                st.sel.clear();
                for (const auto& c : added) { notes.push_back(c); st.sel.push_back(c.id); }
            }

            if (keep)
            {
                const MidiNote& hn = notes[hi];
                const float x1 = beatToX(hn.startBeat);
                const float x2 = beatToX(hn.startBeat + hn.lengthBeats);
                st.selectedId = dragTarget;
                st.dragId = dragTarget;
                st.dragMode = ((x2 - x1) > 12.0f && m.x > x2 - 6.0f) ? 2 : 1;
                st.dragGrabBeat = xToBeat(m.x) - hn.startBeat;
                st.dragStartPitch = hn.pitch;
                st.orig.clear();
                for (const auto& n : notes)
                    if (n.trackIndex == track && isSel(n.id))
                        st.orig.push_back({ n.id, n.pitch, n.startBeat, n.lengthBeats });
                if (st.auditionOn && st.audition) st.audition(hn.pitch, hn.velocity, false);
            }
        }
        else if (st.tool == 0)
        {
            MidiNote n;
            n.trackIndex = track;
            n.pitch = yToPitch(m.y);
            if (st.lockScale && st.scaleType != 0)
                n.pitch = NearestInScale(n.pitch, st.scaleRoot, st.scaleType);
            float b = std::max(0.0f, xToBeat(m.x));
            if (snapEnabled && snapBeat > 0.0f) b = std::floor(b / snapBeat) * snapBeat;
            n.startBeat = b;
            n.lengthBeats = st.lastLength;
            n.velocity = st.defaultVel;

            const std::vector<int> pitches = ChordPitches(n.pitch, st.chordType, st.chordInv, st.scaleRoot, st.scaleType);
            st.sel.clear();
            st.orig.clear();
            int anchorId = -1;
            for (size_t ci = 0; ci < pitches.size(); ++ci)
            {
                MidiNote c = n;
                c.id = st.nextId++;
                c.pitch = pitches[ci];
                notes.push_back(c);
                st.sel.push_back(c.id);
                st.orig.push_back({ c.id, c.pitch, c.startBeat, c.lengthBeats });
                if (ci == 0) { anchorId = c.id; st.dragStartPitch = c.pitch; }
                if (st.auditionOn && st.audition) st.audition(c.pitch, c.velocity, ci > 0);
            }
            st.selectedId = anchorId;
            st.dragId = anchorId;
            st.dragMode = st.drawExtend ? 6 : 1;
            st.pressX = m.x;
            st.lastAudPitch = st.dragStartPitch;
            st.dragGrabBeat = xToBeat(m.x) - b;
        }
        else
        {
            if (!additive)
            {
                st.sel.clear();
                st.selectedId = -1;
            }
            st.dragMode = 3;
            st.dragId = -1;
            st.boxStart = ImVec2(m.x - o.x, m.y - o.y);
        }
    }

    // ---- mouse: right click deletes ----
    if (inGrid && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        const int hi = hitNote();
        if (hi >= 0) eraseNoteAt(hi);
    }

    // ---- mouse: drag ----
    bool boxActive = false;
    ImVec2 boxMin(0, 0), boxMax(0, 0);

    if (st.dragMode == 5)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (inGrid)
            {
                const int hi = hitNote();
                if (hi >= 0) eraseNoteAt(hi);
            }
        }
        else st.dragMode = 0;
    }
    else if (st.dragMode == 3)
    {
        const ImVec2 a(o.x + st.boxStart.x, o.y + st.boxStart.y);
        boxMin = ImVec2(std::min(a.x, m.x), std::min(a.y, m.y));
        boxMax = ImVec2(std::max(a.x, m.x), std::max(a.y, m.y));

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            boxActive = true;
        }
        else
        {
            for (const auto& n : notes)
            {
                if (n.trackIndex != track) continue;
                const float nx1 = beatToX(n.startBeat);
                const float nx2 = beatToX(n.startBeat + n.lengthBeats);
                const float ny1 = pitchToY(n.pitch);
                const float ny2 = ny1 + nh;
                if (nx2 >= boxMin.x && nx1 <= boxMax.x && ny2 >= boxMin.y && ny1 <= boxMax.y && !isSel(n.id))
                    st.sel.push_back(n.id);
            }
            st.selectedId = st.sel.empty() ? -1 : st.sel.back();
            st.dragMode = 0;
        }
    }
    else if (st.dragId != -1)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const PianoRollOrig* anchor = nullptr;
            for (const auto& oo : st.orig)
                if (oo.id == st.dragId) { anchor = &oo; break; }

            if (anchor)
            {
                if (st.dragMode == 1)
                {
                    float minS = anchor->start;
                    int minP = 127, maxP = 0;
                    for (const auto& oo : st.orig)
                    {
                        minS = std::min(minS, oo.start);
                        minP = std::min(minP, oo.pitch);
                        maxP = std::max(maxP, oo.pitch);
                    }
                    float d = snapB(xToBeat(m.x) - st.dragGrabBeat) - anchor->start;
                    d = std::max(d, -minS);
                    int dp = yToPitch(m.y) - st.dragStartPitch;
                    dp = std::clamp(dp, -minP, 127 - maxP);
                    if (st.auditionOn && st.audition && anchor->pitch + dp != st.lastAudPitch)
                    {
                        st.lastAudPitch = anchor->pitch + dp;
                        st.audition(st.lastAudPitch, 0.8f, false);
                    }

                    for (const auto& oo : st.orig)
                    {
                        if (MidiNote* n = findNote(oo.id))
                        {
                            n->startBeat = std::max(0.0f, oo.start + d);
                            n->pitch = std::clamp(oo.pitch + dp, 0, 127);
                        }
                    }
                }
                else if (st.dragMode == 2)
                {
                    const float newLen = std::max(minLen, snapB(xToBeat(m.x) - anchor->start));
                    const float dLen = newLen - anchor->len;
                    for (const auto& oo : st.orig)
                    {
                        if (MidiNote* n = findNote(oo.id))
                            n->lengthBeats = std::max(minLen, oo.len + dLen);
                    }
                    st.lastLength = newLen;
                }
                else if (st.dragMode == 6)
                {
                    if (std::fabs(m.x - st.pressX) > 4.0f)
                    {
                        const float newLen = std::max(minLen, snapB(xToBeat(m.x) - anchor->start));
                        const float dLen = newLen - anchor->len;
                        for (const auto& oo : st.orig)
                        {
                            if (MidiNote* n = findNote(oo.id))
                                n->lengthBeats = std::max(minLen, oo.len + dLen);
                        }
                        st.lastLength = newLen;
                    }
                }
            }
        }
        else
        {
            st.dragId = -1;
            st.dragMode = 0;
        }
    }

    // ---- keyboard ----
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput)
    {
        if (io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) opSelectAll();
            else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) opCopy();
            else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) opPaste();
            else if (ImGui::IsKeyPressed(ImGuiKey_D, false)) opDuplicate();
        }
        else
        {
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) st.tool = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) st.tool = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) st.tool = 2;
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) opQuantize();
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true)) opLenDelta(-1);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true)) opLenDelta(1);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { st.sel.clear(); st.selectedId = -1; }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
            opDelete();

        if (!st.sel.empty() && st.dragId == -1)
        {
            const float stepB = (snapEnabled && snapBeat > 0.0f) ? snapBeat : 0.25f;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
                forTarget([&](MidiNote& n) { n.startBeat = std::max(0.0f, n.startBeat - stepB); });
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
                forTarget([&](MidiNote& n) { n.startBeat += stepB; });
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            {
                opTranspose(io.KeyCtrl ? 12 : 1);
                auditionPrimary();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            {
                opTranspose(io.KeyCtrl ? -12 : -1);
                auditionPrimary();
            }
        }
    }

    // ---------------------------------------------------------------- drawing: rows
    const bool useScale = st.showScale && st.scaleType != 0;
    for (int p = 0; p < 128; ++p)
    {
        const int k = p % 12;
        const bool black = (k == 1 || k == 3 || k == 6 || k == 8 || k == 10);
        const float y = pitchToY(p);

        ImU32 col = black ? IM_COL32(33, 34, 38, 255) : IM_COL32(46, 47, 53, 255);
        if (useScale)
        {
            if (!InScale(p, st.scaleRoot, st.scaleType))
                col = IM_COL32(26, 27, 30, 255);
            else if (((p - st.scaleRoot) % 12 + 12) % 12 == 0)
                col = IM_COL32(58, 72, 86, 255);
            else
                col = black ? IM_COL32(44, 47, 54, 255) : IM_COL32(56, 58, 66, 255);
        }
        dl->AddRectFilled(ImVec2(o.x + keyW, y), ImVec2(o.x + W, y + nh), col);
        dl->AddLine(ImVec2(o.x + keyW, y), ImVec2(o.x + W, y),
            k == 0 ? IM_COL32(12, 12, 14, 255) : IM_COL32(28, 29, 33, 255), k == 0 ? 1.5f : 1.0f);
    }

    if (inGrid)
    {
        const float y = pitchToY(yToPitch(m.y));
        dl->AddRectFilled(ImVec2(o.x + keyW, y), ImVec2(o.x + W, y + nh), IM_COL32(255, 255, 255, 14));
    }

    // ---- grid ----
    {
        const float step = (snapEnabled && snapBeat > 0.0f && snapBeat * ppb >= 8.0f) ? snapBeat : 1.0f;
        const int steps = static_cast<int>(totalBeats / step);
        for (int i = 0; i <= steps; ++i)
        {
            const float b = i * step;
            const float x = beatToX(b);
            const bool bar = std::fmod(b, 4.0f) < 0.0001f;
            const bool beat = std::fabs(b - std::round(b)) < 0.0001f;
            dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + H),
                bar ? IM_COL32(105, 108, 118, 255) : beat ? IM_COL32(70, 72, 82, 255) : IM_COL32(52, 54, 62, 255));
        }
    }

    // ---- ghost notes (other tracks) ----
    if (st.ghostNotes)
    {
        for (const auto& n : notes)
        {
            if (n.trackIndex == track) continue;
            const float x1 = beatToX(n.startBeat);
            const float x2 = beatToX(n.startBeat + n.lengthBeats);
            const float y1 = pitchToY(n.pitch);
            dl->AddRectFilled(ImVec2(x1, y1 + 2), ImVec2(std::max(x2, x1 + 2.0f), y1 + nh - 2),
                IM_COL32(150, 160, 185, 55), 2.0f);
        }
    }

    // ---- notes ----
    for (const auto& n : notes)
    {
        if (n.trackIndex != track) continue;
        const float x1 = beatToX(n.startBeat);
        const float x2 = beatToX(n.startBeat + n.lengthBeats);
        const float y1 = pitchToY(n.pitch);
        const bool s = isSel(n.id);

        const ImU32 col = s ? IM_COL32(255, 196, 74, 255) : VelocityColor(n.velocity);
        dl->AddRectFilled(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 - 1, y1 + nh - 1), col, 2.0f);
        dl->AddRect(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 - 1, y1 + nh - 1),
            s ? IM_COL32(255, 240, 190, 255) : IM_COL32(12, 14, 20, 255), 2.0f);

        if ((x2 - x1) > 12.0f)
            dl->AddLine(ImVec2(x2 - 4, y1 + 3), ImVec2(x2 - 4, y1 + nh - 3), IM_COL32(0, 0, 0, 90));

        if (st.showNames && nh >= 13.0f && (x2 - x1) > 30.0f)
        {
            char nm[8];
            MidiPitchName(n.pitch, nm, sizeof(nm));
            dl->AddText(ImVec2(x1 + 4, y1 + (nh - ImGui::GetFontSize()) * 0.5f),
                IM_COL32(10, 12, 18, 230), nm);
        }
    }

    // ---- box selection ----
    if (boxActive)
    {
        dl->AddRectFilled(boxMin, boxMax, IM_COL32(255, 196, 74, 40));
        dl->AddRect(boxMin, boxMax, IM_COL32(255, 196, 74, 220));
    }

    // ---- playhead ----
    {
        const float x = beatToX(playheadBeat);
        dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + H), IM_COL32(255, 80, 70, 255), 2.0f);
    }

    // ---- keyboard (sticks to the left edge while scrolling) ----
    for (int p = 0; p < 128; ++p)
    {
        const int k = p % 12;
        const bool black = (k == 1 || k == 3 || k == 6 || k == 8 || k == 10);
        const float y = pitchToY(p);

        bool selectedKey = false;
        bool soundingKey = false;
        for (const auto& n : notes)
        {
            if (n.trackIndex != track || n.pitch != p) continue;
            if (isSel(n.id)) selectedKey = true;
            if (playheadBeat >= n.startBeat && playheadBeat < n.startBeat + n.lengthBeats) soundingKey = true;
        }

        ImU32 col = black ? IM_COL32(28, 28, 30, 255) : IM_COL32(226, 227, 230, 255);
        if (soundingKey) col = IM_COL32(110, 220, 130, 255);
        else if (selectedKey) col = IM_COL32(255, 196, 74, 255);
        else if (inGrid && yToPitch(m.y) == p) col = black ? IM_COL32(70, 72, 80, 255) : IM_COL32(255, 255, 255, 255);

        const float kw = black ? keyW * 0.62f : keyW;
        dl->AddRectFilled(ImVec2(keyX, y), ImVec2(keyX + keyW, y + nh), IM_COL32(226, 227, 230, 255));
        dl->AddRectFilled(ImVec2(keyX, y), ImVec2(keyX + kw, y + nh), col);
        dl->AddRect(ImVec2(keyX, y), ImVec2(keyX + keyW, y + nh), IM_COL32(80, 82, 90, 255));
        if (k == 0)
        {
            char nm[8];
            MidiPitchName(p, nm, sizeof(nm));
            dl->AddText(ImVec2(keyX + keyW - 26.0f, y + (nh - ImGui::GetFontSize()) * 0.5f),
                IM_COL32(20, 20, 24, 255), nm);
        }
    }

    // ---- ruler (sticks to the top while scrolling) ----
    {
        dl->AddRectFilled(ImVec2(o.x, rulerTop), ImVec2(o.x + W, rulerTop + rulerH), IM_COL32(38, 39, 44, 255));
        dl->AddLine(ImVec2(o.x, rulerTop + rulerH), ImVec2(o.x + W, rulerTop + rulerH), IM_COL32(12, 12, 14, 255));

        const int beatsTotal = static_cast<int>(totalBeats);
        for (int b = 0; b <= beatsTotal; ++b)
        {
            const float x = beatToX(static_cast<float>(b));
            const bool bar = (b % 4 == 0);
            if (!bar && ppb < 24.0f) continue;
            dl->AddLine(ImVec2(x, rulerTop + (bar ? 2.0f : rulerH * 0.6f)),
                ImVec2(x, rulerTop + rulerH),
                bar ? IM_COL32(170, 172, 182, 255) : IM_COL32(92, 94, 104, 255));
            if (bar)
            {
                char t[16];
                std::snprintf(t, sizeof(t), "%d", b / 4 + 1);
                dl->AddText(ImVec2(x + 4.0f, rulerTop + 3.0f), IM_COL32(215, 217, 225, 255), t);
            }
        }

        const float px = beatToX(playheadBeat);
        dl->AddTriangleFilled(ImVec2(px - 5, rulerTop + 2), ImVec2(px + 5, rulerTop + 2),
            ImVec2(px, rulerTop + 11), IM_COL32(255, 80, 70, 255));

        // corner above the keyboard
        dl->AddRectFilled(ImVec2(keyX, rulerTop), ImVec2(keyX + keyW, rulerTop + rulerH), IM_COL32(32, 33, 37, 255));
        dl->AddRect(ImVec2(keyX, rulerTop), ImVec2(keyX + keyW, rulerTop + rulerH), IM_COL32(12, 12, 14, 255));
        if (inGrid)
        {
            char nm[8], info[48];
            MidiPitchName(yToPitch(m.y), nm, sizeof(nm));
            std::snprintf(info, sizeof(info), "%s", nm);
            dl->AddText(ImVec2(keyX + 6.0f, rulerTop + 3.0f), IM_COL32(255, 196, 74, 255), info);
        }
    }

    st.scrollX = ImGui::GetScrollX();
    ImGui::EndChild();

    // ---------------------------------------------------------------- velocity lane
    if (st.showVelocity)
    {
        ImGui::BeginChild("PianoRollVelocity", ImVec2(0, st.velLaneH), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetScrollX(st.scrollX);

        const ImVec2 vo = ImGui::GetCursorScreenPos();
        const float VH = std::max(24.0f, st.velLaneH - 30.0f);
        ImGui::InvisibleButton("##vel", ImVec2(W, VH), ImGuiButtonFlags_MouseButtonLeft);
        const bool vHover = ImGui::IsItemHovered();
        ImDrawList* vd = ImGui::GetWindowDrawList();
        auto vx = [&](float b) { return vo.x + keyW + b * ppb; };

        vd->AddRectFilled(vo, ImVec2(vo.x + W, vo.y + VH), IM_COL32(28, 29, 33, 255));

        const int beatsTotal = static_cast<int>(totalBeats);
        for (int b = 0; b <= beatsTotal; ++b)
        {
            const bool bar = (b % 4 == 0);
            if (!bar && ppb < 24.0f) continue;
            const float x = vx(static_cast<float>(b));
            vd->AddLine(ImVec2(x, vo.y), ImVec2(x, vo.y + VH),
                bar ? IM_COL32(80, 82, 92, 255) : IM_COL32(48, 50, 58, 255));
        }
        for (int q = 1; q < 4; ++q)
        {
            const float y = vo.y + VH * q / 4.0f;
            vd->AddLine(ImVec2(vo.x + keyW, y), ImVec2(vo.x + W, y), IM_COL32(42, 44, 50, 255));
        }

        // pick a velocity bar
        if (vHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            int best = -1;
            float bestDist = 7.0f;
            for (const auto& n : notes)
            {
                if (n.trackIndex != track) continue;
                const float d = std::fabs(m.x - (vx(n.startBeat) + 2.0f));
                if (d <= bestDist) { bestDist = d; best = n.id; }
            }
            st.velDragId = best;
            st.velPaint = (best == -1);
        }

        if (st.velPaint)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float pv = std::clamp(1.0f - (m.y - vo.y) / VH, 0.01f, 1.0f);
                for (auto& n : notes)
                    if (n.trackIndex == track && std::fabs(m.x - (vx(n.startBeat) + 2.0f)) <= 7.0f) n.velocity = pv;
            }
            else st.velPaint = false;
        }

        if (st.velDragId != -1)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float v = std::clamp(1.0f - (m.y - vo.y) / VH, 0.01f, 1.0f);
                if (isSel(st.velDragId))
                {
                    for (auto& n : notes)
                        if (n.trackIndex == track && isSel(n.id)) n.velocity = v;
                }
                else if (MidiNote* n = findNote(st.velDragId))
                {
                    n->velocity = v;
                }
            }
            else st.velDragId = -1;
        }

        for (const auto& n : notes)
        {
            if (n.trackIndex != track) continue;
            const float x = vx(n.startBeat);
            const float top = vo.y + VH * (1.0f - n.velocity);
            const bool s = isSel(n.id);
            const ImU32 col = s ? IM_COL32(255, 196, 74, 255) : VelocityColor(n.velocity);
            vd->AddRectFilled(ImVec2(x, top), ImVec2(x + 5.0f, vo.y + VH), col);
            vd->AddCircleFilled(ImVec2(x + 2.5f, top), 3.5f, col);
        }

        // label (sticks to the left edge)
        const float lx = vo.x + ImGui::GetScrollX();
        vd->AddRectFilled(ImVec2(lx, vo.y), ImVec2(lx + keyW, vo.y + VH), IM_COL32(32, 33, 37, 255));
        vd->AddRect(ImVec2(lx, vo.y), ImVec2(lx + keyW, vo.y + VH), IM_COL32(12, 12, 14, 255));
        vd->AddText(ImVec2(lx + 5.0f, vo.y + 4.0f), IM_COL32(200, 202, 210, 255), "Velocity");
        vd->AddText(ImVec2(lx + 5.0f, vo.y + 20.0f), IM_COL32(120, 122, 132, 255), "100%");
        vd->AddText(ImVec2(lx + 5.0f, vo.y + VH - 16.0f), IM_COL32(120, 122, 132, 255), "0%");

        ImGui::EndChild();
    }
}

// Fallback synth used when the track has no VST3 instrument (offline render helper).
// out: interleaved stereo float.
inline void RenderMidiBuiltin(
    const std::vector<MidiNote>& notes,
    int track,
    float bpm,
    unsigned sampleRate,
    double totalSeconds,
    std::vector<float>& out)
{
    const double pi = 3.14159265358979323846;
    const size_t frames = static_cast<size_t>(std::ceil(totalSeconds * sampleRate));
    out.assign(frames * 2, 0.0f);
    if (bpm <= 0.0f) return;

    const double spb = 60.0 / bpm;
    const size_t attack = std::max<size_t>(1, static_cast<size_t>(0.005 * sampleRate));
    const size_t release = std::max<size_t>(1, static_cast<size_t>(0.12 * sampleRate));

    for (const auto& n : notes)
    {
        if (n.trackIndex != track) continue;

        size_t f0 = static_cast<size_t>(std::max(0.0, n.startBeat * spb) * sampleRate);
        size_t f1 = static_cast<size_t>(std::max(0.0, (n.startBeat + n.lengthBeats) * spb) * sampleRate);
        size_t end = std::min(frames, f1 + release);
        double freq = 440.0 * std::pow(2.0, (n.pitch - 69) / 12.0);
        double inc = freq / sampleRate;
        double phase = 0.0;
        float amp = n.velocity * 0.22f;

        for (size_t f = f0; f < end; ++f)
        {
            float env = std::min(1.0f, static_cast<float>(f - f0) / static_cast<float>(attack));
            if (f >= f1)
                env *= 1.0f - static_cast<float>(f - f1) / static_cast<float>(release);

            float saw = static_cast<float>(2.0 * phase - 1.0);
            float sine = static_cast<float>(std::sin(2.0 * pi * phase));
            float s = (sine * 0.7f + saw * 0.3f) * amp * env;

            out[f * 2] += s;
            out[f * 2 + 1] += s;

            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

    for (float& s : out) s = std::clamp(s, -1.0f, 1.0f);
}