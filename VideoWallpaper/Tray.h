// Tray.h
// Wraps Shell_NotifyIcon to show a system tray icon with a right-click
// context menu (Play / Pause / Stop / Change Video / Exit). Actual command
// handling lives in App.cpp; this class only owns the icon's lifetime and
// menu display, and reports which command was chosen via a callback.

#pragma once

#include <Windows.h>
#include <functional>
#include <string>

enum class TrayCommand
{
    Play,
    Pause,
    Stop,
    ChangeVideo,
    Exit
};

class Tray
{
public:
    Tray() = default;
    ~Tray();

    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // ownerWindow receives the tray's callback messages (WM_APP_TRAY).
    // hIcon is NOT owned by Tray; caller must keep it alive for the tray's
    // lifetime (typically an icon loaded once from the resource script).
    bool Initialize(HWND ownerWindow, HINSTANCE hInstance, HICON hIcon, const std::wstring& tooltip);

    void Shutdown();

    // Call this from the owner window's WndProc when it receives the
    // tray's callback message (WM_APP_TRAY), passing lParam through.
    // Displays the popup menu if the message indicates a right-click and
    // invokes onCommand with the user's selection.
    void HandleCallback(LPARAM lParam, const std::function<void(TrayCommand)>& onCommand);

    // Updates the tooltip text shown when hovering the tray icon.
    void SetTooltip(const std::wstring& tooltip);

    // The custom window message used for tray icon callbacks.
    static constexpr UINT WM_APP_TRAY = WM_APP + 1;

private:
    NOTIFYICONDATAW m_nid = {};
    HWND m_ownerWindow = nullptr;
    bool m_added = false;
};
