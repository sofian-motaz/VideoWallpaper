// Tray.cpp
#include "Tray.h"
#include "resource.h"

Tray::~Tray()
{
    Shutdown();
}

bool Tray::Initialize(HWND ownerWindow, HINSTANCE hInstance, HICON hIcon, const std::wstring& tooltip)
{
    UNREFERENCED_PARAMETER(hInstance);

    m_ownerWindow = ownerWindow;

    m_nid = {};
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = ownerWindow;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_APP_TRAY;
    m_nid.hIcon = hIcon;
    wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);

    m_added = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;

    // Request modern behavior (balloon/notification style) where available.
    m_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &m_nid);

    return m_added;
}

void Tray::SetTooltip(const std::wstring& tooltip)
{
    if (!m_added)
        return;

    wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void Tray::HandleCallback(LPARAM lParam, const std::function<void(TrayCommand)>& onCommand)
{
    UINT mouseMsg = LOWORD(lParam);

    if (mouseMsg != WM_RBUTTONUP && mouseMsg != WM_CONTEXTMENU)
        return;

    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
        return;

    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::Play) + 1, L"Play");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::Pause) + 1, L"Pause");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::Stop) + 1, L"Stop");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::ChangeVideo) + 1, L"Change Video...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::Exit) + 1, L"Exit");

    // Required so the popup menu closes properly when it loses focus.
    SetForegroundWindow(m_ownerWindow);

    UINT clicked = static_cast<UINT>(TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        pt.x, pt.y, 0, m_ownerWindow, nullptr));

    PostMessage(m_ownerWindow, WM_NULL, 0, 0); // ensures menu dismisses cleanly
    DestroyMenu(menu);

    if (clicked != 0 && onCommand)
    {
        onCommand(static_cast<TrayCommand>(clicked - 1));
    }
}

void Tray::Shutdown()
{
    if (m_added)
    {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
    }
}
