// Utilities.h
// Small collection of stateless helper functions used across the project:
// - Friendly error dialogs
// - LibVLC installation discovery
// - File extension / path validation
// - DPI-aware screen size helper
//
// Kept deliberately free of globals; everything here is a pure function or
// takes an explicit HWND owner for any UI it shows.

#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <optional>

namespace Utilities
{
    // Shows a modal MB_ICONERROR dialog with the given title/message.
    // Safe to call with hOwner == nullptr (dialog will be owned by the desktop).
    void ShowErrorDialog(HWND hOwner, const std::wstring& title, const std::wstring& message);

    // Shows a modal MB_ICONINFORMATION dialog.
    void ShowInfoDialog(HWND hOwner, const std::wstring& title, const std::wstring& message);

    // Returns true if the extension of filePath (case-insensitive) is one of
    // the video formats we claim to support: mp4, avi, mkv, wmv, mov.
    bool IsSupportedVideoFile(const std::wstring& filePath);

    // Returns true if the file exists and is readable.
    bool FileExists(const std::wstring& filePath);

    // Attempts to locate a working libvlc.dll / libvlccore.dll pair.
    // Search order:
    //   1. ".\libvlc\" next to the executable (a portable "vendored" copy)
    //   2. Executable's own directory
    //   3. HKLM/HKCU "SOFTWARE\VideoLAN\VLC" InstallDir registry key
    //   4. Common install paths (Program Files, Program Files (x86))
    //   5. The current PATH environment variable
    //
    // On success, returns the directory containing libvlc.dll.
    // On failure, returns std::nullopt.
    std::optional<std::wstring> FindLibVlcDirectory();

    // Prepends dir to the process PATH and calls SetDllDirectory so that
    // subsequent LoadLibrary("libvlc.dll") calls succeed.
    void PrependToDllSearchPath(const std::wstring& dir);

    // Returns the directory the running executable lives in, no trailing slash.
    std::wstring GetExecutableDirectory();

    // Returns the full virtual screen size (all monitors combined).
    SIZE GetVirtualScreenSize();

    // Returns the primary monitor size.
    SIZE GetPrimaryScreenSize();

    // Converts a narrow UTF-8 std::string to std::wstring.
    std::wstring Utf8ToWide(const std::string& utf8);

    // Converts std::wstring to a narrow UTF-8 std::string (used for libvlc,
    // which expects UTF-8 char* paths).
    std::string WideToUtf8(const std::wstring& wide);

    // Opens a standard Win32 "Open File" dialog filtered to supported video
    // types. Returns the selected path, or empty string if the user cancelled.
    std::wstring OpenVideoFileDialog(HWND hOwner);

    // Writes a line to OutputDebugString with a "[VideoWallpaper] " prefix.
    // Compiled out (no-op) in Release builds via NDEBUG.
    void DebugLog(const std::wstring& message);
}
