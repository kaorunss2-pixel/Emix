#pragma once
// EasyMix Studio: Cubase-like look (dark graphite panels, orange accent) and small UI helpers.
#include <imgui.h>
#include <algorithm>

void ApplyCubaseStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    // モダンな余白と角丸
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(12.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* colors = style.Colors;
    // 深いダークブルーグレーをベースに
    colors[ImGuiCol_WindowBg]           = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg]            = ImVec4(0.10f, 0.11f, 0.14f, 0.80f); // 半透明（グラスモーフィズム風）
    colors[ImGuiCol_PopupBg]            = ImVec4(0.12f, 0.13f, 0.17f, 0.95f);
    colors[ImGuiCol_Border]             = ImVec4(0.25f, 0.27f, 0.35f, 0.60f);
    
    // コントロール系
    colors[ImGuiCol_FrameBg]            = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.20f, 0.23f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.25f, 0.28f, 0.38f, 1.00f);
    
    // ボタン（Cubaseのオレンジ/シアンアクセント）
    colors[ImGuiCol_Button]             = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.90f, 0.45f, 0.10f, 0.80f); // ホバーでオレンジに発光
    colors[ImGuiCol_ButtonActive]       = ImVec4(1.00f, 0.55f, 0.15f, 1.00f);
    
    // ヘッダーとタブ
    colors[ImGuiCol_Header]             = ImVec4(0.20f, 0.75f, 0.90f, 0.30f); // シアン
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.20f, 0.75f, 0.90f, 0.50f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.20f, 0.75f, 0.90f, 0.70f);
    
    colors[ImGuiCol_Tab]                = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.30f, 0.80f, 0.95f, 0.50f);
    colors[ImGuiCol_TabSelected]        = ImVec4(0.20f, 0.65f, 0.85f, 0.80f);

    // スライダーとチェックマーク
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.95f, 0.50f, 0.15f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(1.00f, 0.65f, 0.25f, 1.00f);
    colors[ImGuiCol_CheckMark]          = ImVec4(0.20f, 0.90f, 0.80f, 1.00f); // ネオングリーン
    
    // テキスト
    colors[ImGuiCol_Text]               = ImVec4(0.92f, 0.94f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.50f, 0.55f, 0.65f, 1.00f);
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