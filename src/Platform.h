#pragma once
// EasyMix (macOS): small compatibility layer so the Windows-style call sites in main.cpp keep working.
//  - OPENFILENAMEA / GetOpenFileNameA / GetSaveFileNameA  -> NSOpenPanel / NSSavePanel
//  - MessageBoxA                                          -> NSAlert
//  - glfwGetWin32Window                                   -> NSWindow of the GLFW window
//  - strcpy_s                                             -> bounded copy
//  - MoveFileExA (MOVEFILE_REPLACE_EXISTING)              -> std::filesystem::rename
// C++ only (do not include from .mm files; they use PlatformApi.h).
#include "PlatformApi.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef __APPLE__
#error "Platform.h is for macOS only."
#endif

#include <objc/objc.h>   // BOOL (GLFW's Cocoa header pulls it in as well, so do not typedef it ourselves)

using HWND = void*;
typedef unsigned long DWORD;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#ifndef CALLBACK
#define CALLBACK
#endif

// ---- file dialogs -------------------------------------------------------------------------
enum
{
    OFN_FILEMUSTEXIST   = 0x00001000,
    OFN_PATHMUSTEXIST   = 0x00000800,
    OFN_OVERWRITEPROMPT = 0x00000002
};

struct OPENFILENAMEA
{
    DWORD       lStructSize = 0;
    HWND        hwndOwner = nullptr;
    const char* lpstrFilter = nullptr;
    DWORD       nFilterIndex = 0;
    char*       lpstrFile = nullptr;
    DWORD       nMaxFile = 0;
    DWORD       Flags = 0;
    const char* lpstrDefExt = nullptr;
};

inline void PlatformCopyOut(const std::string& path, char* dst, DWORD cap)
{
    if (!dst || cap == 0)
        return;
    std::snprintf(dst, cap, "%s", path.c_str());
}

inline BOOL GetOpenFileNameA(OPENFILENAMEA* d)
{
    std::string path;
    if (!d || !plat::OpenFileDialog(d->hwndOwner, d->lpstrFilter, path))
        return FALSE;
    PlatformCopyOut(path, d->lpstrFile, d->nMaxFile);
    return TRUE;
}

inline BOOL GetSaveFileNameA(OPENFILENAMEA* d)
{
    std::string path;
    if (!d || !plat::SaveFileDialog(d->hwndOwner, d->lpstrFilter, d->lpstrDefExt, path))
        return FALSE;
    PlatformCopyOut(path, d->lpstrFile, d->nMaxFile);
    return TRUE;
}

// ---- message box --------------------------------------------------------------------------
enum
{
    MB_OK            = 0x00000000,
    MB_YESNOCANCEL   = 0x00000003,
    MB_ICONERROR     = 0x00000010,
    MB_ICONQUESTION  = 0x00000020
};

enum
{
    IDOK     = 1,
    IDCANCEL = 2,
    IDYES    = 6,
    IDNO     = 7
};

inline int MessageBoxA(HWND, const char* text, const char* caption, unsigned type)
{
    if ((type & 0xF) == MB_YESNOCANCEL)
    {
        switch (plat::ConfirmSave(caption, text))
        {
        case 1:  return IDYES;
        case 2:  return IDNO;
        default: return IDCANCEL;
        }
    }

    plat::ErrorBox(caption, text);
    return IDOK;
}

// ---- misc ---------------------------------------------------------------------------------
inline int strcpy_s(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0)
        return 1;
    std::snprintf(dst, cap, "%s", src ? src : "");
    return 0;
}

// MoveFileExA with MOVEFILE_REPLACE_EXISTING: std::filesystem::rename replaces an
// existing target on macOS (rename(2)) and Windows alike, so it matches the
// atomic temp-file -> final-file pattern used for WAV/project saves.
#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#endif
#ifndef MOVEFILE_WRITE_THROUGH
#define MOVEFILE_WRITE_THROUGH 0x00000008
#endif

inline BOOL MoveFileExA(const char* from, const char* to, DWORD /*flags*/)
{
    if (!from || !to)
        return FALSE;
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return ec ? FALSE : TRUE;
}
