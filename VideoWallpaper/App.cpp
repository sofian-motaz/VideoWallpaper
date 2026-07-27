// App.cpp
#include "App.h"
#include "resource.h"
#include "Utilities.h"
#include <CommCtrl.h>
#include <Shlwapi.h>

#pragma comment(lib, "Comctl32.lib")

namespace
{
    constexpr wchar_t kMainWindowClassName[] = L"VideoWallpaperControlPanel";
    constexpr wchar_t kMainWindowTitle[] = L"Video Wallpaper";
    constexpr wchar_t kRunRegistryKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValueName[] = L"VideoWallpaperApp";

    // Simple fixed layout metrics for the control panel (DPI-scaled once at
    // creation time via GetDpiForWindow where available; falls back to 96
    // DPI assumption on systems without that API, e.g. Windows 7 RTM).
    struct Layout
    {
        int margin = 12;
        int rowHeight = 24;
        int spacing = 8;
        int buttonWidth = 90;
        int windowWidth = 420;
        int windowHeight = 330;
    };

    std::wstring GetSettingsFilePath()
    {
        return Utilities::GetExecutableDirectory() + L"\\settings.ini";
    }
}

App::~App()
{
    if (m_hAppIcon) DestroyIcon(m_hAppIcon);
    if (m_hTrayIcon) DestroyIcon(m_hTrayIcon);
}

bool App::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = App::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kMainWindowClassName;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;

    if (RegisterClassExW(&wc) == 0)
    {
        Utilities::ShowErrorDialog(nullptr, L"Video Wallpaper", L"Failed to register window class.");
        return false;
    }

    Layout layout;
    RECT windowRect = { 0, 0, layout.windowWidth, layout.windowHeight };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);

    m_hwnd = CreateWindowExW(
        0,
        kMainWindowClassName,
        kMainWindowTitle,
        (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, this);

    if (m_hwnd == nullptr)
    {
        Utilities::ShowErrorDialog(nullptr, L"Video Wallpaper", L"Failed to create the main window.");
        return false;
    }

    m_hAppIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    m_hTrayIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_TRAYICON));

    m_desktop = std::make_unique<Desktop>();
    m_player = std::make_unique<VideoPlayer>();
    m_tray = std::make_unique<Tray>();

    if (!m_tray->Initialize(m_hwnd, hInstance, m_hTrayIcon, L"Video Wallpaper - no video loaded"))
    {
        Utilities::DebugLog(L"Tray icon failed to initialize (non-fatal).");
    }

    // Locate LibVLC before attempting anything else; this is the single
    // most common failure mode for end users.
    auto vlcDir = Utilities::FindLibVlcDirectory();
    if (!vlcDir.has_value())
    {
        Utilities::ShowErrorDialog(
            m_hwnd,
            L"VLC Not Found",
            L"Video Wallpaper requires LibVLC (from VLC media player) to play video.\n\n"
            L"VLC was not found on this system. Please do one of the following:\n\n"
            L"  1. Install VLC media player (64-bit or 32-bit to match this app) from\n"
            L"     https://www.videolan.org/vlc/ and restart Video Wallpaper, or\n\n"
            L"  2. Copy libvlc.dll, libvlccore.dll, and the \"plugins\" folder from an\n"
            L"     existing VLC installation into a folder named \"libvlc\" next to\n"
            L"     VideoWallpaper.exe.\n\n"
            L"The application will now continue in a limited mode; video playback\n"
            L"controls will be disabled until LibVLC is found.");
    }
    else
    {
        Utilities::PrependToDllSearchPath(vlcDir.value());

        std::wstring initError;
        if (!m_player->Initialize(vlcDir.value(), initError))
        {
            Utilities::ShowErrorDialog(m_hwnd, L"LibVLC Initialization Failed", initError);
        }
    }

    CreateControls(m_hwnd);
    LoadSettings();
    RefreshControlStates();

    bool wallpaperOk = EnsureWallpaperReady(m_hwnd);
    if (!wallpaperOk)
    {
        Utilities::ShowErrorDialog(
            m_hwnd,
            L"Desktop Integration Failed",
            L"Video Wallpaper could not attach to the Windows desktop (WorkerW window).\n"
            L"This can happen on unusual shell configurations. The application will still\n"
            L"run, but video will not be visible behind your desktop icons.");
    }

    if (m_player->IsInitialized())
    {
        m_player->SetRenderTarget(m_desktop->GetRenderHostWindow());
        m_player->SetLooping(m_loop);
        m_player->SetMuted(m_muted);
        m_player->SetScaleMode(m_scaleMode);
    }

    SetTimer(m_hwnd, TIMER_ID_PLAYER_TICK, 500, nullptr);
    SetTimer(m_hwnd, TIMER_ID_WORKERW_WATCHDOG, 5000, nullptr);

    if (m_autostart && !m_currentFile.empty() && m_player->IsInitialized())
    {
        std::wstring err;
        if (m_player->Open(m_currentFile, err))
        {
            m_player->Play();
            UpdateStatusText(L"Playing (autostart): " + m_currentFile);
        }
        else
        {
            Utilities::ShowErrorDialog(m_hwnd, L"Playback Error", err);
        }
    }

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    return true;
}

bool App::EnsureWallpaperReady(HWND hwnd)
{
    if (m_wallpaperReady && m_desktop->GetRenderHostWindow() != nullptr && IsWindow(m_desktop->GetRenderHostWindow()))
        return true;

    SIZE screen = Utilities::GetVirtualScreenSize();
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);

    m_wallpaperReady = m_desktop->Initialize(m_hInstance, x, y, screen.cx, screen.cy);
    UNREFERENCED_PARAMETER(hwnd);
    return m_wallpaperReady;
}

void App::RecreateDesktopAndReopen(HWND hwnd)
{
    m_desktop->Shutdown();
    m_wallpaperReady = false;

    if (EnsureWallpaperReady(hwnd) && m_player->IsInitialized())
    {
        m_player->SetRenderTarget(m_desktop->GetRenderHostWindow());
        if (!m_currentFile.empty())
        {
            std::wstring err;
            if (m_player->Open(m_currentFile, err))
            {
                m_player->Play();
            }
        }
    }
}

void App::CreateControls(HWND hwnd)
{
    Layout layout;
    int x = layout.margin;
    int y = layout.margin;
    int fullWidth = layout.windowWidth - layout.margin * 2;

    auto nextRow = [&](int extra = 0) { y += layout.rowHeight + layout.spacing + extra; };

    // File path display (read-only edit-style static, clipped with ellipsis)
    m_lblFilePath = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"No video selected",
        WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS | SS_NOTIFY,
        x, y, fullWidth, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_STATIC_FILEPATH, m_hInstance, nullptr);
    nextRow();

    // Open button
    m_btnOpen = CreateWindowExW(0, L"BUTTON", L"Open Video...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, fullWidth, layout.rowHeight + 6, hwnd, (HMENU)(INT_PTR)IDC_BTN_OPEN, m_hInstance, nullptr);
    nextRow(6);

    // Play / Pause / Stop row
    int btnW = (fullWidth - layout.spacing * 2) / 3;
    m_btnPlay = CreateWindowExW(0, L"BUTTON", L"Play",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, btnW, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_PLAY, m_hInstance, nullptr);
    m_btnPause = CreateWindowExW(0, L"BUTTON", L"Pause",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + btnW + layout.spacing, y, btnW, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_PAUSE, m_hInstance, nullptr);
    m_btnStop = CreateWindowExW(0, L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + (btnW + layout.spacing) * 2, y, btnW, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_BTN_STOP, m_hInstance, nullptr);
    nextRow(4);

    // Loop / Autostart / Mute checkboxes
    int chkW = fullWidth / 3;
    m_chkLoop = CreateWindowExW(0, L"BUTTON", L"Loop",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, chkW, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_CHK_LOOP, m_hInstance, nullptr);
    m_chkAutostart = CreateWindowExW(0, L"BUTTON", L"Autostart with Windows",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x + chkW, y, chkW + 40, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_CHK_AUTOSTART, m_hInstance, nullptr);
    m_chkMute = CreateWindowExW(0, L"BUTTON", L"Mute",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x + chkW * 2 + 40, y, chkW - 40, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_CHK_MUTE, m_hInstance, nullptr);
    nextRow(10);

    // Scale mode group box + radio buttons
    HWND group = CreateWindowExW(0, L"BUTTON", L"Scale Mode",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, fullWidth, layout.rowHeight * 2 + layout.spacing, hwnd, (HMENU)(INT_PTR)IDC_GROUP_SCALE, m_hInstance, nullptr);
    UNREFERENCED_PARAMETER(group);

    int radioY = y + 20;
    int radioW = fullWidth / 3 - 8;
    m_radioFit = CreateWindowExW(0, L"BUTTON", L"Fit",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        x + 10, radioY, radioW, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_RADIO_FIT, m_hInstance, nullptr);
    m_radioFill = CreateWindowExW(0, L"BUTTON", L"Fill",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        x + 10 + radioW, radioY, radioW, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_RADIO_FILL, m_hInstance, nullptr);
    m_radioStretch = CreateWindowExW(0, L"BUTTON", L"Stretch",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        x + 10 + radioW * 2, radioY, radioW, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_RADIO_STRETCH, m_hInstance, nullptr);
    nextRow(layout.rowHeight + layout.spacing + 10);

    // Status line
    m_lblStatus = CreateWindowExW(0, L"STATIC", L"Ready.",
        WS_CHILD | WS_VISIBLE,
        x, y, fullWidth, layout.rowHeight, hwnd, (HMENU)(INT_PTR)IDC_STATIC_STATUS, m_hInstance, nullptr);

    // Apply a nicer default font (Segoe UI where available) to all controls.
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
        static HFONT s_segoe = CreateFontIndirectW(&ncm.lfMessageFont);
        if (s_segoe) hFont = s_segoe;
    }

    HWND controls[] = {
        m_lblFilePath, m_btnOpen, m_btnPlay, m_btnPause, m_btnStop,
        m_chkLoop, m_chkAutostart, m_chkMute, group,
        m_radioFit, m_radioFill, m_radioStretch, m_lblStatus
    };
    for (HWND c : controls)
    {
        if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    }
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* app = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    else
    {
        app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app != nullptr)
        return app->HandleMessage(hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        OnCommand(hwnd, wParam);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_ID_PLAYER_TICK)
        {
            OnTimerTick(hwnd);
        }
        else if (wParam == TIMER_ID_WORKERW_WATCHDOG)
        {
            OnWorkerWatchdog(hwnd);
        }
        return 0;

    case Tray::WM_APP_TRAY:
        if (m_tray)
        {
            m_tray->HandleCallback(lParam, [this](TrayCommand cmd) { OnTrayCommand(cmd); });
        }
        return 0;

    case WM_DISPLAYCHANGE:
    {
        if (m_desktop)
        {
            SIZE screen = Utilities::GetVirtualScreenSize();
            int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            m_desktop->Resize(x, y, screen.cx, screen.cy);
        }
        return 0;
    }

    case WM_SYSCOMMAND:
        // Minimize to tray instead of the taskbar.
        if ((wParam & 0xFFF0) == SC_MINIMIZE)
        {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        // "X" hides to tray rather than exiting, matching the behavior of
        // most wallpaper utilities (video keeps playing in the background).
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        SaveSettings();
        KillTimer(hwnd, TIMER_ID_PLAYER_TICK);
        KillTimer(hwnd, TIMER_ID_WORKERW_WATCHDOG);
        if (m_tray) m_tray->Shutdown();
        if (m_player) m_player->Shutdown();
        if (m_desktop) m_desktop->Shutdown();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void App::OnCommand(HWND hwnd, WPARAM wParam)
{
    WORD id = LOWORD(wParam);
    WORD notifyCode = HIWORD(wParam);

    if (notifyCode != BN_CLICKED)
        return;

    switch (id)
    {
    case IDC_BTN_OPEN: OnOpenVideo(hwnd); break;
    case IDC_BTN_PLAY: OnPlay(); break;
    case IDC_BTN_PAUSE: OnPause(); break;
    case IDC_BTN_STOP: OnStop(); break;
    case IDC_CHK_LOOP: OnLoopToggled(); break;
    case IDC_CHK_MUTE: OnMuteToggled(); break;
    case IDC_CHK_AUTOSTART:
        m_autostart = (IsDlgButtonChecked(hwnd, IDC_CHK_AUTOSTART) == BST_CHECKED);
        SetAutostartRegistryEntry(m_autostart);
        break;
    case IDC_RADIO_FIT:
        m_scaleMode = ScaleMode::Fit;
        OnScaleModeChanged();
        break;
    case IDC_RADIO_FILL:
        m_scaleMode = ScaleMode::Fill;
        OnScaleModeChanged();
        break;
    case IDC_RADIO_STRETCH:
        m_scaleMode = ScaleMode::Stretch;
        OnScaleModeChanged();
        break;
    default:
        break;
    }
}

void App::OnOpenVideo(HWND hwnd)
{
    std::wstring path = Utilities::OpenVideoFileDialog(hwnd);
    if (path.empty())
        return;

    if (!m_player->IsInitialized())
    {
        Utilities::ShowErrorDialog(hwnd, L"Playback Unavailable",
            L"LibVLC is not available, so video cannot be played. See the earlier "
            L"dialog for instructions on installing VLC.");
        return;
    }

    if (!EnsureWallpaperReady(hwnd))
    {
        Utilities::ShowErrorDialog(hwnd, L"Desktop Integration Failed",
            L"Could not attach to the desktop WorkerW window.");
        return;
    }

    m_player->SetRenderTarget(m_desktop->GetRenderHostWindow());

    std::wstring err;
    if (!m_player->Open(path, err))
    {
        Utilities::ShowErrorDialog(hwnd, L"Cannot Open Video", err);
        UpdateStatusText(L"Error: " + err);
        return;
    }

    m_currentFile = path;
    m_player->SetLooping(m_loop);
    m_player->SetMuted(m_muted);
    m_player->SetScaleMode(m_scaleMode);
    m_player->Play();

    UpdateFilePathLabel();
    UpdateStatusText(L"Playing: " + path);

    if (m_tray) m_tray->SetTooltip(L"Video Wallpaper - " + path);

    SaveSettings();
}

void App::OnPlay()
{
    if (!m_player || !m_player->IsInitialized())
        return;

    if (m_currentFile.empty())
    {
        OnOpenVideo(m_hwnd);
        return;
    }

    m_player->Play();
    UpdateStatusText(L"Playing: " + m_currentFile);
}

void App::OnPause()
{
    if (!m_player || !m_player->IsInitialized())
        return;

    m_player->Pause();
    UpdateStatusText(m_player->GetState() == PlaybackState::Paused ? L"Paused." : L"Playing: " + m_currentFile);
}

void App::OnStop()
{
    if (!m_player || !m_player->IsInitialized())
        return;

    m_player->Stop();
    UpdateStatusText(L"Stopped.");
}

void App::OnScaleModeChanged()
{
    if (m_player && m_player->IsInitialized())
    {
        m_player->SetScaleMode(m_scaleMode);
    }
    SaveSettings();
}

void App::OnLoopToggled()
{
    m_loop = (IsDlgButtonChecked(m_hwnd, IDC_CHK_LOOP) == BST_CHECKED);
    if (m_player) m_player->SetLooping(m_loop);
    SaveSettings();
}

void App::OnMuteToggled()
{
    m_muted = (IsDlgButtonChecked(m_hwnd, IDC_CHK_MUTE) == BST_CHECKED);
    if (m_player) m_player->SetMuted(m_muted);
    SaveSettings();
}

void App::OnTrayCommand(TrayCommand cmd)
{
    switch (cmd)
    {
    case TrayCommand::Play: OnPlay(); break;
    case TrayCommand::Pause: OnPause(); break;
    case TrayCommand::Stop: OnStop(); break;
    case TrayCommand::ChangeVideo:
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        OnOpenVideo(m_hwnd);
        break;
    case TrayCommand::Exit:
        DestroyWindow(m_hwnd);
        break;
    }
}

void App::OnTimerTick(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    if (m_player && m_player->IsInitialized())
    {
        m_player->Tick();
    }
}

void App::OnWorkerWatchdog(HWND hwnd)
{
    // If explorer.exe restarted (crash, "Restart Explorer" from Task
    // Manager, shell extension misbehaving, etc.) our WorkerW parent may
    // have disappeared. Detect that and rebuild the wallpaper pipeline.
    if (m_desktop && !IsWindow(m_desktop->GetRenderHostWindow()))
    {
        Utilities::DebugLog(L"Render host window lost; rebuilding desktop integration.");
        RecreateDesktopAndReopen(hwnd);
    }
    else if (m_desktop)
    {
        // Cheap re-validation in case WorkerW itself was recreated while our
        // render host window happens to still be alive as an orphan.
        m_desktop->ReattachToWorkerW();
    }
}

void App::UpdateStatusText(const std::wstring& text)
{
    if (m_lblStatus)
        SetWindowTextW(m_lblStatus, text.c_str());
}

void App::UpdateFilePathLabel()
{
    if (m_lblFilePath)
        SetWindowTextW(m_lblFilePath, m_currentFile.empty() ? L"No video selected" : m_currentFile.c_str());
}

void App::RefreshControlStates()
{
    CheckDlgButton(m_hwnd, IDC_CHK_LOOP, m_loop ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hwnd, IDC_CHK_MUTE, m_muted ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hwnd, IDC_CHK_AUTOSTART, m_autostart ? BST_CHECKED : BST_UNCHECKED);

    CheckDlgButton(m_hwnd, IDC_RADIO_FIT, m_scaleMode == ScaleMode::Fit ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hwnd, IDC_RADIO_FILL, m_scaleMode == ScaleMode::Fill ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hwnd, IDC_RADIO_STRETCH, m_scaleMode == ScaleMode::Stretch ? BST_CHECKED : BST_UNCHECKED);

    UpdateFilePathLabel();

    bool vlcOk = m_player && m_player->IsInitialized();
    EnableWindow(m_btnOpen, vlcOk);
    EnableWindow(m_btnPlay, vlcOk);
    EnableWindow(m_btnPause, vlcOk);
    EnableWindow(m_btnStop, vlcOk);
}

void App::LoadSettings()
{
    std::wstring iniPath = GetSettingsFilePath();

    wchar_t buffer[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(L"Settings", L"LastFile", L"", buffer, MAX_PATH * 2, iniPath.c_str());
    m_currentFile = buffer;

    m_loop = GetPrivateProfileIntW(L"Settings", L"Loop", 1, iniPath.c_str()) != 0;
    m_muted = GetPrivateProfileIntW(L"Settings", L"Muted", 1, iniPath.c_str()) != 0;
    m_autostart = IsAutostartRegistryEnabled();

    int scaleModeInt = GetPrivateProfileIntW(L"Settings", L"ScaleMode", 0, iniPath.c_str());
    m_scaleMode = static_cast<ScaleMode>(scaleModeInt);
}

void App::SaveSettings()
{
    std::wstring iniPath = GetSettingsFilePath();

    WritePrivateProfileStringW(L"Settings", L"LastFile", m_currentFile.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"Loop", m_loop ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"Muted", m_muted ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"ScaleMode", std::to_wstring(static_cast<int>(m_scaleMode)).c_str(), iniPath.c_str());
}

void App::SetAutostartRegistryEntry(bool enabled)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegistryKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (enabled)
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring cmd = std::wstring(L"\"") + exePath + L"\" /autostart";
        RegSetValueExW(hKey, kRunValueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(cmd.c_str()),
            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(hKey, kRunValueName);
    }

    RegCloseKey(hKey);
}

bool App::IsAutostartRegistryEnabled() const
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegistryKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    bool exists = RegQueryValueExW(hKey, kRunValueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);

    return exists;
}

int App::RunMessageLoop()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
