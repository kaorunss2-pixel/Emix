#pragma once
// EasyMix Studio - Modern UI Extras
// Header-only ImGui components. Keeps the original public API.
//  - SpectrumAnalyzer : realtime FFT spectrum of the master output
//  - DrawEqGraph      : EQ response curve + spectrum, drag band nodes (freq/gain), wheel = Q, double click = reset gain
//  - PeakHold / DrawMeterBar : dB scaled level meter with peak hold and clip colours
//  - Toasts           : fading notifications (replaces the red status text)
//  - CommandPalette   : Ctrl+P search & run any command
//  - DrawShortcutsWindow : F1 cheat sheet
// Visual refresh: midnight surfaces, cool-blue EQ accents, clearer meter/toast contrast.
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "Equalizer.h"

// ------------------------------------------------------------------ spectrum
struct SpectrumAnalyzer
{
    static constexpr int N = 2048;          // FFT size (power of two)
    float ring[N] = {};
    std::atomic<unsigned> w{ 0 };
    std::vector<float> db = std::vector<float>(N / 2, -100.0f);
    float rate = 44100.0f;

    // audio thread: interleaved stereo
    void push(const float* st, unsigned frames)
    {
        unsigned p = w.load(std::memory_order_relaxed);
        for (unsigned i = 0; i < frames; ++i)
            ring[(p + i) & (N - 1)] = 0.5f * (st[i * 2] + st[i * 2 + 1]);
        w.store(p + frames, std::memory_order_release);
    }

    // UI thread, once per frame
    void update()
    {
        static std::vector<float> re(N), im(N);
        const unsigned p = w.load(std::memory_order_acquire);
        const float pi = 3.14159265f;
        for (int i = 0; i < N; ++i)
        {
            const float win = 0.5f - 0.5f * std::cos(2.0f * pi * i / (N - 1));
            re[i] = ring[(p + i) & (N - 1)] * win;
            im[i] = 0.0f;
        }
        // bit reversal
        for (int i = 1, j = 0; i < N; ++i)
        {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= N; len <<= 1)
        {
            const float ang = -2.0f * pi / len;
            const float wr = std::cos(ang), wi = std::sin(ang);
            for (int i = 0; i < N; i += len)
            {
                float cr = 1.0f, ci = 0.0f;
                for (int k = 0; k < len / 2; ++k)
                {
                    const int a = i + k, b = i + k + len / 2;
                    const float tr = re[b] * cr - im[b] * ci;
                    const float ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                    const float ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = ncr;
                }
            }
        }
        for (int k = 0; k < N / 2; ++k)
        {
            const float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]) / (N / 4.0f);
            const float d = 20.0f * std::log10(mag + 1.0e-7f);
            db[k] = d > db[k] ? d : db[k] * 0.88f + d * 0.12f;
        }
    }
};

// ------------------------------------------------------------------ EQ graph
inline float BiquadMagnitudeDb(const Biquad& b, float f, float sr)
{
    const float w = 2.0f * 3.14159265f * f / sr;
    const float c1 = std::cos(w), s1 = std::sin(w);
    const float c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
    const float nr = b.b0 + b.b1 * c1 + b.b2 * c2;
    const float ni = -(b.b1 * s1 + b.b2 * s2);
    const float dr = 1.0f + b.a1 * c1 + b.a2 * c2;
    const float di = -(b.a1 * s1 + b.a2 * s2);
    const float n = nr * nr + ni * ni, d = dr * dr + di * di;
    return 10.0f * std::log10(std::max(n, 1.0e-12f) / std::max(d, 1.0e-12f));
}

// returns true while the user changes something
inline bool DrawEqGraph(const char* id, Equalizer& eq, ImVec2 size,
    const SpectrumAnalyzer* spec = nullptr, float sr = 44100.0f)
{
    ImGui::PushID(id);
    if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0.0f) size.y = 180.0f;

    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##eqg", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 m = ImGui::GetIO().MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float lo = 20.0f, hi = 20000.0f, range = 18.0f;
    auto fx = [&](float f) { return p.x + std::log(f / lo) / std::log(hi / lo) * size.x; };
    auto xf = [&](float x) { return lo * std::pow(hi / lo, std::clamp((x - p.x) / size.x, 0.0f, 1.0f)); };
    auto gy = [&](float g) { return p.y + size.y * (0.5f - g / (2.0f * range)); };
    auto yg = [&](float y) { return std::clamp((0.5f - (y - p.y) / size.y) * 2.0f * range, -range, range); };

    static const ImU32 cols[Equalizer::BandCount] = {
        IM_COL32(255, 105, 125, 255), IM_COL32(255, 181, 92, 255),
        IM_COL32(116, 220, 170, 255), IM_COL32(86, 190, 255, 255),
        IM_COL32(190, 135, 255, 255) };

    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(13, 17, 25, 255), 10.0f);

    // grid
    const float fl[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    for (float f : fl)
    {
        const float x = fx(f);
        dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + size.y), IM_COL32(36, 47, 63, 190));
        char t[16];
        if (f >= 1000.0f) std::snprintf(t, sizeof(t), "%gk", f / 1000.0f); else std::snprintf(t, sizeof(t), "%g", f);
        dl->AddText(ImVec2(x + 2, p.y + size.y - 14), IM_COL32(133, 151, 174, 255), t);
    }
    for (int g = -12; g <= 12; g += 6)
    {
        const float y = gy(static_cast<float>(g));
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), g == 0 ? IM_COL32(91, 119, 150, 220) : IM_COL32(30, 40, 55, 190));
        char t[8]; std::snprintf(t, sizeof(t), "%+d", g);
        dl->AddText(ImVec2(p.x + 3, y - 13), IM_COL32(133, 151, 174, 255), t);
    }

    // spectrum (-90 .. 0 dB)
    if (spec)
    {
        const int bins = SpectrumAnalyzer::N / 2;
        const float binHz = sr / SpectrumAnalyzer::N;
        ImVec2 prev(p.x, p.y + size.y);
        for (int x = 0; x <= static_cast<int>(size.x); x += 2)
        {
            const float f = xf(p.x + x);
            const int k = std::clamp(static_cast<int>(f / binHz), 1, bins - 1);
            const float v = std::clamp((spec->db[k] + 90.0f) / 90.0f, 0.0f, 1.0f);
            const ImVec2 q(p.x + x, p.y + size.y - v * size.y * 0.9f);
            dl->AddRectFilled(ImVec2(q.x, q.y), ImVec2(q.x + 2, p.y + size.y), IM_COL32(53, 173, 235, 48));
        }
        (void)prev;
    }

    // response curve
    Biquad tmp[Equalizer::BandCount];
    for (int b = 0; b < Equalizer::BandCount; ++b)
        tmp[b].setPeaking(sr, eq.frequencies[b], eq.gains[b], eq.qValues[b]);

    std::vector<ImVec2> pts;
    for (int x = 0; x <= static_cast<int>(size.x); x += 3)
    {
        const float f = xf(p.x + x);
        float d = 0.0f;
        if (eq.enabled)
            for (int b = 0; b < Equalizer::BandCount; ++b)
                d += BiquadMagnitudeDb(tmp[b], f, sr);
        pts.emplace_back(p.x + x, std::clamp(gy(d), p.y, p.y + size.y));
    }
    if (pts.size() > 1)
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()),
            eq.enabled ? IM_COL32(80, 205, 255, 255) : IM_COL32(100, 116, 139, 255), 0, 2.8f);

    // nodes
    ImGuiStorage* stg = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID("drag");
    int drag = stg->GetInt(key, -1);
    bool changed = false;

    int nearest = -1;
    float best = 14.0f * 14.0f;
    for (int b = 0; b < Equalizer::BandCount; ++b)
    {
        const float dx = m.x - fx(eq.frequencies[b]), dy = m.y - gy(eq.gains[b]);
        if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; nearest = b; }
    }

    if (ImGui::IsItemActivated() && nearest >= 0) drag = nearest;
    if (drag >= 0)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            eq.frequencies[drag] = std::clamp(xf(m.x), 20.0f, 20000.0f);
            eq.gains[drag] = std::round(yg(m.y) * 10.0f) / 10.0f;
            changed = true;
        }
        else drag = -1;
    }
    if (hovered && nearest >= 0)
    {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { eq.gains[nearest] = 0.0f; changed = true; }
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            eq.qValues[nearest] = std::clamp(eq.qValues[nearest] * (1.0f + 0.12f * wheel), 0.1f, 10.0f);
            changed = true;
        }
    }
    stg->SetInt(key, drag);

    for (int b = 0; b < Equalizer::BandCount; ++b)
    {
        const ImVec2 c(fx(eq.frequencies[b]), gy(eq.gains[b]));
        const bool act = (b == drag) || (drag < 0 && b == nearest && hovered);
        dl->AddCircleFilled(c, act ? 9.0f : 7.0f, cols[b]);
        dl->AddCircle(c, act ? 9.0f : 7.0f, IM_COL32(235, 245, 255, 220), 0, 2.0f);
        char t[8]; std::snprintf(t, sizeof(t), "%d", b + 1);
        dl->AddText(ImVec2(c.x - 3, c.y - 7), IM_COL32(10, 10, 10, 255), t);
    }
    if (drag >= 0 || (hovered && nearest >= 0))
    {
        const int b = drag >= 0 ? drag : nearest;
        ImGui::SetTooltip("Band %d  %.0f Hz  %+.1f dB  Q %.2f\nWheel: Q   Double click: reset gain",
            b + 1, eq.frequencies[b], eq.gains[b], eq.qValues[b]);
    }

    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(53, 75, 101, 230), 10.0f);
    ImGui::PopID();
    return changed;
}

// ------------------------------------------------------------------ meters
struct PeakHold
{
    float hold = 0.0f, timer = 0.0f;
    bool clip = false;
    void update(float level, float dt)
    {
        if (level >= hold) { hold = level; timer = 1.2f; }
        else { timer -= dt; if (timer < 0.0f) hold = std::max(0.0f, hold - dt * 0.6f); }
        if (level > 1.0f) clip = true;
    }
};

inline float MeterPos(float lin)           // -60 dB .. +6 dB -> 0..1
{
    const float d = 20.0f * std::log10(std::max(lin, 1.0e-5f));
    return std::clamp((d + 60.0f) / 66.0f, 0.0f, 1.0f);
}

// vertical bar. Click on it to clear the clip indicator.
inline void DrawMeterBar(ImVec2 size, float level, PeakHold& ph, bool drawScale = true)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##meter", size);
    if (ImGui::IsItemClicked()) ph.clip = false;
    ph.update(level, ImGui::GetIO().DeltaTime);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(12, 18, 26, 255));

    const float v = MeterPos(level);
    const float zeroPos = MeterPos(1.0f), warnPos = MeterPos(0.5f);
    auto seg = [&](float a, float b, ImU32 col)
        {
            if (v <= a) return;
            const float t = std::min(v, b);
            dl->AddRectFilled(ImVec2(p.x, p.y + size.y * (1.0f - t)), ImVec2(p.x + size.x, p.y + size.y * (1.0f - a)), col);
        };
    seg(0.0f, warnPos, IM_COL32(64, 210, 145, 255));
    seg(warnPos, zeroPos, IM_COL32(255, 190, 75, 255));
    seg(zeroPos, 1.0f, IM_COL32(255, 83, 105, 255));

    const float hy = p.y + size.y * (1.0f - MeterPos(ph.hold));
    dl->AddLine(ImVec2(p.x, hy), ImVec2(p.x + size.x, hy), IM_COL32(255, 255, 255, 230), 2.0f);

    if (drawScale)
        for (int d : { -48, -24, -12, -6, 0 })
        {
            const float y = p.y + size.y * (1.0f - (d + 60.0f) / 66.0f);
            dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + 4.0f, y), IM_COL32(200, 200, 210, 150));
        }
    if (ph.clip)
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + 5.0f), IM_COL32(255, 40, 40, 255));
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(58, 78, 103, 255));
}

// ------------------------------------------------------------------ toasts
struct Toasts
{
    struct Item { std::string text; double born; int kind; };   // 0 info, 1 ok, 2 error
    std::vector<Item> items;

    static bool Has(const std::string& s, const char* k) { return s.find(k) != std::string::npos; }

    void Push(const std::string& text)
    {
        if (text.empty()) return;
        int kind = 0;
        if (Has(text, "fail") || Has(text, "Could not") || Has(text, "Invalid") || Has(text, "No ") ||
            Has(text, "Select a") || Has(text, "not available") || Has(text, "Unsupported"))
            kind = 2;
        else if (Has(text, "success") || Has(text, "saved") || Has(text, "exported") ||
            Has(text, "loaded") || Has(text, "imported") || Has(text, "Stems"))
            kind = 1;
        items.push_back({ text, ImGui::GetTime(), kind });
        if (items.size() > 6) items.erase(items.begin());
    }

    void Draw()
    {
        const double now = ImGui::GetTime();
        items.erase(std::remove_if(items.begin(), items.end(),
            [&](const Item& i) { return now - i.born > 4.0; }), items.end());

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        float y = vp->WorkPos.y + vp->WorkSize.y - 16.0f;
        for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i)
        {
            const Item& it = items[i];
            const double age = now - it.born;
            const float a = static_cast<float>(std::clamp(std::min(age / 0.2, (4.0 - age) / 0.6), 0.0, 1.0));
            const ImVec2 ts = ImGui::CalcTextSize(it.text.c_str());
            const ImVec2 sz(ts.x + 28.0f, ts.y + 18.0f);
            const ImVec2 mn(vp->WorkPos.x + vp->WorkSize.x - sz.x - 16.0f, y - sz.y);
            const ImVec2 mx(mn.x + sz.x, mn.y + sz.y);
            const ImU32 acc = it.kind == 2 ? IM_COL32(255, 91, 110, 255)
                : it.kind == 1 ? IM_COL32(65, 215, 150, 255) : IM_COL32(75, 175, 255, 255);
            auto A = [&](ImU32 c) { return (c & 0x00FFFFFF) | (static_cast<ImU32>(((c >> 24) & 0xFF) * a) << 24); };
            dl->AddRectFilled(mn, mx, A(IM_COL32(19, 27, 39, 248)), 9.0f);
            dl->AddRectFilled(mn, ImVec2(mn.x + 4.0f, mx.y), A(acc), 9.0f);
            dl->AddText(ImVec2(mn.x + 17.0f, mn.y + 9.0f), A(IM_COL32(235, 237, 242, 255)), it.text.c_str());
            y -= sz.y + 8.0f;
        }
    }
};

// ------------------------------------------------------------------ command palette
struct Command
{
    std::string name;
    std::string shortcut;
    std::function<void()> run;
};

struct CommandPalette
{
    bool open = false;
    bool justOpened = false;
    char filter[64] = {};
    int sel = 0;
    std::vector<Command> cmds;

    static std::string Lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // every space separated word must appear in the name
    static bool MatchAll(const std::string& f, const std::string& n)
    {
        size_t p = 0;
        while (p < f.size())
        {
            size_t q = f.find(' ', p);
            if (q == std::string::npos) q = f.size();
            if (q > p && n.find(f.substr(p, q - p)) == std::string::npos) return false;
            p = q + 1;
        }
        return true;
    }

    void Draw()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
        {
            open = true; justOpened = true; filter[0] = 0; sel = 0;
        }
        if (!open) return;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 90.0f),
            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f));
        ImGui::SetNextWindowFocus();
        if (ImGui::Begin("##palette", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
        {
            if (justOpened) { ImGui::SetKeyboardFocusHere(); justOpened = false; }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##pf", "Search commands...   Enter: Run   Esc: Close", filter, sizeof(filter));

            const std::string f = Lower(filter);
            std::vector<int> hit;
            for (int i = 0; i < static_cast<int>(cmds.size()); ++i)
                if (MatchAll(f, Lower(cmds[i].name)))
                    hit.push_back(i);

            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++sel;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) --sel;
            sel = hit.empty() ? 0 : std::clamp(sel, 0, static_cast<int>(hit.size()) - 1);

            int run = -1;
            ImGui::BeginChild("##plist", ImVec2(0.0f, std::min(320.0f, hit.size() * 24.0f + 6.0f)));
            for (int k = 0; k < static_cast<int>(hit.size()); ++k)
            {
                const Command& c = cmds[hit[k]];
                if (ImGui::Selectable(c.name.c_str(), k == sel))
                    run = hit[k];
                if (!c.shortcut.empty())
                {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
                    ImGui::TextDisabled("%s", c.shortcut.c_str());
                }
                if (k == sel) ImGui::SetScrollHereY();
            }
            ImGui::EndChild();

            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !hit.empty()) run = hit[sel];
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
            if (run >= 0)
            {
                open = false;
                if (cmds[run].run) cmds[run].run();
            }
        }
        ImGui::End();
    }
};

// ------------------------------------------------------------------ shortcut sheet
inline void DrawShortcutsWindow(bool* open)
{
    if (!open || !*open) return;
    ImGui::SetNextWindowSize(ImVec2(460.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Shortcuts (F1)", open))
    {
        struct Row { const char* k; const char* d; };
        static const Row rows[] = {
            { "Space", "Play / Stop" }, { "Home", "Go to start" }, { "L", "Loop on/off" },
            { "C", "Click (metronome)" }, { "M", "Add marker" }, { "PageUp / PageDown", "Prev / next marker" },
            { "Ctrl+S", "Save project" }, { "Ctrl+Z / Ctrl+Y", "Undo / Redo" },
            { "Ctrl+P", "Command palette" }, { "F1", "This window" },
            { "Ctrl+Wheel", "Zoom timeline / piano roll" }, { "S", "Split selected clip at playhead" },
            { "Ctrl+D", "Duplicate clip / notes" }, { "Del", "Delete clip / notes" },
            { "Alt+Click", "Add volume automation point" }, { "Alt+Right click", "Remove automation point" },
            { "Piano roll D / S / E", "Draw / Select / Erase tool" },
            { "Double click fader/pan", "Reset to default" },
            { "EQ graph: drag", "Move band (freq / gain)" }, { "EQ graph: wheel", "Change Q" },
            { "Ctrl+C / Ctrl+V", "Copy / paste selected clip (at playhead)" },
            { "Ctrl+O / Ctrl+E", "Open project / Export WAV" },
            { "Ctrl + = / Ctrl + -", "Zoom timeline in / out" },
            { "End", "Go to end of content" }, { ", / .", "Nudge playhead 1 beat (Shift: 1 bar)" },
            { "I / O", "Set loop start / end at playhead" },
            { "Piano roll: click keys", "Audition a pitch (drag = glissando)" },
            { "Piano roll: ruler", "Click / drag to move the playhead" },
            { "Piano roll: Alt+drag", "Copy the dragged notes" },
            { "Piano roll: Q / [ / ] / Esc", "Quantize / shorten / lengthen / deselect" },
        };
        if (ImGui::BeginTable("sc", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            for (const Row& r : rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", r.k);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r.d);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}