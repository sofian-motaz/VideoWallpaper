// Desktop.h
// Implements the classic "WorkerW" technique used by wallpaper engines to
// host a window behind the desktop icons but above the actual wallpaper.
//
// The trick (well known in the Windows shell community since Windows Vista):
//   1. Find the "Progman" window.
//   2. Send it the undocumented message 0x052C. This causes explorer.exe to
//      spawn a "WorkerW" window as a sibling used to host icons/wallpaper.
//   3. Enumerate top-level windows to find the WorkerW that is a sibling of
//      the WorkerW/SHELLDLL_DefView pair, OR the WorkerW that has NO
//      SHELLDLL_DefView child (that's the empty one meant for wallpaper
//      content on modern Windows 10/11 builds).
//   4. SetParent() our own borderless layered "render host" window into that
//      WorkerW. Windows now composites it below the icons automatically.
//
// This class owns the creation and lifetime of that render host window; it
// does NOT own the video rendering itself (see VideoPlayer.h) - it simply
// gives VideoPlayer a valid HWND to draw/embed into.

#pragma once

#include <Windows.h>

class Desktop
{
public:
    Desktop() = default;
    ~Desktop();

    Desktop(const Desktop&) = delete;
    Desktop& operator=(const Desktop&) = delete;

    // Performs the WorkerW dance and creates our render host window as a
    // child of it, sized to the given (virtual) screen rectangle.
    // Returns true on success.
    bool Initialize(HINSTANCE hInstance, int x, int y, int width, int height);

    // Tears down the render host window and detaches from WorkerW.
    void Shutdown();

    // Handle to the borderless window that video content is embedded into.
    HWND GetRenderHostWindow() const { return m_renderHost; }

    // Re-parents the render host if explorer.exe has been restarted and a
    // brand new WorkerW was created (call this if playback appears frozen
    // after an explorer crash/restart - App.cpp wires this to a periodic
    // sanity check).
    bool ReattachToWorkerW();

    // Resizes/moves the render host window, e.g. after a display resolution
    // or monitor configuration change (WM_DISPLAYCHANGE).
    void Resize(int x, int y, int width, int height);

private:
    static LRESULT CALLBACK RenderHostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Finds (or triggers creation of) the WorkerW window that sits directly
    // behind the desktop icons. Returns nullptr on failure.
    HWND FindOrCreateWorkerW();

    HINSTANCE m_hInstance = nullptr;
    HWND m_progman = nullptr;
    HWND m_workerW = nullptr;
    HWND m_renderHost = nullptr;
    bool m_classRegistered = false;

    static constexpr wchar_t kRenderHostClassName[] = L"VideoWallpaperRenderHost";
};
