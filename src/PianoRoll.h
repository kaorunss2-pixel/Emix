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
//   - Ruler with bar numbers, Ctrl + mouse wheel zooms horizontally
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    float playheadBeat)
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
            ImGui::TextDisabled("Ctrl+A/C/V/D  Del  Arrows  Ctrl+Wheel zoom  (D/S/E = tools)");
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

            if (additive)
            {
                if (isSel(id)) { removeFromSel(id); keep = false; }
                else { st.sel.push_back(id); st.selectedId = id; }
            }
            else if (!isSel(id))
            {
                selectOnly(id);
            }

            if (keep)
            {
                const MidiNote& hn = notes[hi];
                const float x1 = beatToX(hn.startBeat);
                const float x2 = beatToX(hn.startBeat + hn.lengthBeats);
                st.selectedId = id;
                st.dragId = id;
                st.dragMode = ((x2 - x1) > 12.0f && m.x > x2 - 6.0f) ? 2 : 1;
                st.dragGrabBeat = xToBeat(m.x) - hn.startBeat;
                st.dragStartPitch = hn.pitch;
                st.orig.clear();
                for (const auto& n : notes)
                    if (n.trackIndex == track && isSel(n.id))
                        st.orig.push_back({ n.id, n.pitch, n.startBeat, n.lengthBeats });
            }
        }
        else if (st.tool == 0)
        {
            MidiNote n;
            n.id = st.nextId++;
            n.trackIndex = track;
            n.pitch = yToPitch(m.y);
            if (st.lockScale && st.scaleType != 0)
                n.pitch = NearestInScale(n.pitch, st.scaleRoot, st.scaleType);
            float b = std::max(0.0f, xToBeat(m.x));
            if (snapEnabled && snapBeat > 0.0f) b = std::floor(b / snapBeat) * snapBeat;
            n.startBeat = b;
            n.lengthBeats = st.lastLength;
            n.velocity = st.defaultVel;
            notes.push_back(n);
            selectOnly(n.id);
            st.dragId = n.id;
            st.dragMode = 1;
            st.dragGrabBeat = xToBeat(m.x) - b;
            st.dragStartPitch = n.pitch;
            st.orig.clear();
            st.orig.push_back({ n.id, n.pitch, n.startBeat, n.lengthBeats });
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
                opTranspose(io.KeyCtrl ? 12 : 1);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
                opTranspose(io.KeyCtrl ? -12 : -1);
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

        if (nh >= 15.0f && (x2 - x1) > 34.0f)
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