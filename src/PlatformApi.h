#pragma once
// EasyMix (macOS): thin native-platform API implemented in PlatformMac.mm (Cocoa).
// Pure C++ declarations only, so it can be included from both .cpp and .mm files.
#include <string>

namespace plat
{
    // `filter` uses the Win32 style: "Desc\0*.wav;*.mp3\0Desc2\0*.*\0" (double-NUL terminated).
    // Only the first entry's extensions are used as the allowed file types.
    bool OpenFileDialog(void* ownerWindow, const char* filter, std::string& outPath);
    bool SaveFileDialog(void* ownerWindow, const char* filter, const char* defExt, std::string& outPath);

    // 0 = Cancel, 1 = Save, 2 = Don't Save
    int ConfirmSave(const char* title, const char* message);
    void ErrorBox(const char* title, const char* message);

    // "~/Library/Application Support/EasyMix" (created if missing)
    std::string SupportDir();

    // ---- VST3 plug-in editor window (NSWindow + NSView) ----
    // onClose is called on the main thread (asynchronously) when the user presses the close button.
    void* CreateEditorWindow(const char* title, int width, int height, void (*onClose)(void*), void* user);
    void* EditorContentView(void* window);              // NSView* to hand to IPlugView::attached
    void ResizeEditorWindow(void* window, int width, int height);
    void ShowEditorWindow(void* window);
    void DestroyEditorWindow(void* window);
}
