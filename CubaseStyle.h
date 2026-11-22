#pragma once
// EasyMix Studio: Cubase-like look (dark graphite panels, orange accent) and small UI helpers.
#include <imgui.h>
#include <algorithm>

inline void ApplyCubaseStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowPadding = ImVec2(8.0f, 8.0f);
    s.FramePadding = ImVec2(7.0f, 4.0f);
    s.CellPadding = ImVec2(6.0f, 4.0f);
    s.ItemSpacing = ImVec2(7.0f, 5.0f);
    s.ItemInnerSpacing = ImVec2(5.0f, 4.0f);

    s.WindowRounding = 3.0f;
    s.ChildRounding = 2.0f;
    s.FrameRounding = 2.0f;
    s.PopupRounding = 3.0f;
    s.ScrollbarRounding = 2.0f;
    s.GrabRounding = 2.0f;
    s.TabRounding = 2.0f;

    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;

    s.ScrollbarSize = 12.0f;
    s.GrabMinSize = 10.0f;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    const ImVec4 accent(0.85f, 0.52f, 0.12f, 1.00f);
    const ImVec4 accentHi(0.95f, 0.64f, 0.24f, 1.00f);

    c[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.89f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.46f, 0.47f, 0.50f, 1.00f);

    c[ImGuiCol_WindowBg] = ImVec4(0.150f, 0.155f, 0.165f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.125f, 0.130f, 0.140f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.170f, 0.175f, 0.188f, 0.98f);

    c[ImGuiCol_Border] = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    c[ImGuiCol_FrameBg] = ImVec4(0.090f, 0.095f, 0.104f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.130f, 0.135f, 0.148f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.160f, 0.165f, 0.180f, 1.00f);

    c[ImGuiCol_TitleBg] = ImVec4(0.100f, 0.105f, 0.115f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.190f, 0.200f, 0.218f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.100f, 0.105f, 0.115f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.115f, 0.120f, 0.130f, 1.00f);

    c[ImGuiCol_ScrollbarBg] = ImVec4(0.085f, 0.090f, 0.098f, 1.00f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.31f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.41f, 0.45f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = accent;

    c[ImGuiCol_CheckMark] = accentHi;
    c[ImGuiCol_SliderGrab] = ImVec4(0.72f, 0.74f, 0.78f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = accentHi;

    c[ImGuiCol_Button] = ImVec4(0.255f, 0.265f, 0.285f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.340f, 0.352f, 0.378f, 1.00f);
    c[ImGuiCol_ButtonActive] = accent;

    c[ImGuiCol_Header] = ImVec4(0.250f, 0.270f, 0.310f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.320f, 0.345f, 0.395f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.62f, 0.40f, 0.12f, 1.00f);

    c[ImGuiCol_Separator] = ImVec4(0.07f, 0.07f, 0.08f, 1.00f);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHi;

    c[ImGuiCol_ResizeGrip] = ImVec4(0.40f, 0.41f, 0.45f, 0.30f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.85f, 0.52f, 0.12f, 0.70f);
    c[ImGuiCol_ResizeGripActive] = accentHi;

    c[ImGuiCol_Tab] = ImVec4(0.12f, 0.125f, 0.135f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.32f, 0.36f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);

    c[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.145f, 0.158f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.07f, 0.07f, 0.08f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);

    c[ImGuiCol_TextSelectedBg] = ImVec4(0.85f, 0.52f, 0.12f, 0.35f);
    c[ImGuiCol_NavCursor] = accent;
}

// Track colours (Cubase-like muted palette). alpha: 0..255
inline ImU32 CubaseTrackColor(int index, int alpha = 255)
{
    static const unsigned char pal[12][3] =
    {
        { 73, 135, 196 }, { 86, 170, 120 }, { 196, 128, 64 }, { 170, 92, 160 },
        { 190, 84, 84 },  { 64, 165, 175 }, { 188, 170, 70 }, { 120, 110, 200 },
        { 150, 175, 80 }, { 200, 110, 140 }, { 90, 140, 150 }, { 160, 130, 100 }
    };
    const int i = ((index % 12) + 12) % 12;
    return IM_COL32(pal[i][0], pal[i][1], pal[i][2], alpha);
}

// Button that lights up in `activeCol` while `active` is true (transport / M / S buttons).
inline bool CubaseToggle(const char* label, bool active, ImVec2 size = ImVec2(0, 0),
    ImU32 activeCol = IM_COL32(217, 133, 31, 255))
{
    if (active)
    {
        const ImVec4 col = ImGui::ColorConvertU32ToFloat4(activeCol);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(
            std::min(1.0f, col.x + 0.08f), std::min(1.0f, col.y + 0.08f), std::min(1.0f, col.z + 0.08f), 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.06f, 1.0f));
    }
    const bool r = ImGui::Button(label, size);
    if (active)
        ImGui::PopStyleColor(4);
    return r;
}

// Dark LCD-style read-out (big time / bar display)
inline void CubaseLcd(const char* text, ImVec2 size, float scale, ImU32 col)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(8, 12, 14, 255), 3.0f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(70, 78, 84, 255), 3.0f);

    const float fs = ImGui::GetFontSize() * scale;
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 tp(p.x + (size.x - ts.x * scale) * 0.5f, p.y + (size.y - ts.y * scale) * 0.5f);
    dl->AddText(ImGui::GetFont(), fs, tp, col, text);
}