// main.cpp
// Windows subsystem entry point (no console window). Parses the minimal
// command line ("/autostart"), enforces single-instance via a named mutex,
// and hands off to App for everything else.

#include <Windows.h>
#include <string>
#include "App.h"
#include "Utilities.h"

namespace
{
    // Prevents multiple copies from fighting over the same WorkerW window.
    constexpr wchar_t kSingleInstanceMutexName[] = L"Global\\VideoWallpaper_SingleInstance_Mutex_9F3B2C";
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                       _In_opt_ HINSTANCE hPrevInstance,
                       _In_ LPWSTR lpCmdLine,
                       _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    bool alreadyRunning = (hMutex != nullptr) && (GetLastError() == ERROR_ALREADY_EXISTS);

    if (alreadyRunning)
    {
        Utilities::ShowInfoDialog(nullptr, L"Video Wallpaper",
            L"Video Wallpaper is already running. Look for its icon in the system tray.");
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // We currently only look for the presence of "/autostart" on the command
    // line for logging/telemetry-free diagnostic purposes; actual autostart
    // behavior is driven purely by the persisted "Autostart" setting so the
    // app behaves the same whether launched by the user or by the Run key.
    std::wstring cmdLine(lpCmdLine != nullptr ? lpCmdLine : L"");
    bool launchedByAutostart = cmdLine.find(L"/autostart") != std::wstring::npos;
    Utilities::DebugLog(launchedByAutostart ? L"Launched via autostart." : L"Launched by user.");

    App app;
    int exitCode = 0;

    if (app.Initialize(hInstance, nCmdShow))
    {
        exitCode = app.RunMessageLoop();
    }
    else
    {
        exitCode = 1;
    }

    if (hMutex)
    {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return exitCode;
}
