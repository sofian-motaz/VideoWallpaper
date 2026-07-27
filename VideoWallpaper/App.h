// App.h
// Owns the application's main (hidden-by-default) control window, the Win32
// UI controls, and coordinates the three subsystems:
//   - Desktop      (WorkerW embedding)
//   - VideoPlayer  (LibVLC playback)
//   - Tray         (system tray icon + menu)
//
// The "main window" here is a normal small control panel window (Open/Play/
// Pause/Stop/Loop/Autostart/Fit/Fill/Stretch + file path label). It is a
// perfectly ordinary top-level window - NOT the wallpaper surface itself.
// The wallpaper surface is Desktop::GetRenderHostWindow(), embedded into
// WorkerW, which is what makes this different from "just open a media
// player window".

#pragma once

#include <Windows.h>
#include <memory>
#include <string>

#include "Desktop.h"
#include "VideoPlayer.h"
#include "Tray.h"

class App
{
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Creates the control window, initializes subsystems, and shows the UI
    // (unless started with /autostart and a previously remembered video,
    // in which case the control window can be minimized to tray directly).
    // Returns false on unrecoverable startup failure.
    bool Initialize(HINSTANCE hInstance, int nCmdShow);

    // Standard Win32 message loop. Returns the exit code for WinMain.
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls(HWND hwnd);
    void LayoutControls(HWND hwnd);
    void OnCommand(HWND hwnd, WPARAM wParam);
    void OnOpenVideo(HWND hwnd);
    void OnPlay();
    void OnPause();
    void OnStop();
    void OnScaleModeChanged();
    void OnLoopToggled();
    void OnMuteToggled();
    void OnTrayCommand(TrayCommand cmd);
    void OnTimerTick(HWND hwnd);
    void OnWorkerWatchdog(HWND hwnd);
    void UpdateStatusText(const std::wstring& text);
    void UpdateFilePathLabel();
    void RefreshControlStates();
    bool EnsureWallpaperReady(HWND hwnd);
    void RecreateDesktopAndReopen(HWND hwnd);

    // Persists/reads simple settings (last file, loop, autostart, scale
    // mode, mute) to an INI file next to the executable, so the app can
    // restore state on next launch (used together with the Autostart
    // checkbox / registry Run key).
    void LoadSettings();
    void SaveSettings();
    void SetAutostartRegistryEntry(bool enabled);
    bool IsAutostartRegistryEnabled() const;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;

    // Child controls
    HWND m_btnOpen = nullptr;
    HWND m_btnPlay = nullptr;
    HWND m_btnPause = nullptr;
    HWND m_btnStop = nullptr;
    HWND m_chkLoop = nullptr;
    HWND m_chkAutostart = nullptr;
    HWND m_chkMute = nullptr;
    HWND m_radioFit = nullptr;
    HWND m_radioFill = nullptr;
    HWND m_radioStretch = nullptr;
    HWND m_lblFilePath = nullptr;
    HWND m_lblStatus = nullptr;

    HICON m_hAppIcon = nullptr;
    HICON m_hTrayIcon = nullptr;

    std::unique_ptr<Desktop> m_desktop;
    std::unique_ptr<VideoPlayer> m_player;
    std::unique_ptr<Tray> m_tray;

    std::wstring m_currentFile;
    bool m_loop = true;
    bool m_muted = true;
    bool m_autostart = false;
    ScaleMode m_scaleMode = ScaleMode::Fit;

    bool m_wallpaperReady = false;
};
