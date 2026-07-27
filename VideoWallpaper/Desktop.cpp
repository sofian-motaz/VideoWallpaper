// Desktop.cpp
#include "Desktop.h"
#include "Utilities.h"

constexpr wchar_t Desktop::kRenderHostClassName[];

namespace
{
    // Data passed through EnumWindows when looking for the correct WorkerW.
    struct EnumContext
    {
        HWND foundWorkerW = nullptr;
    };

    // Callback used to locate the WorkerW window that should host our
    // wallpaper content. Two shapes exist in the wild:
    //
    //  Older Windows 7/8/10 (icons hosted by a WorkerW that has a
    //  SHELLDLL_DefView sibling):
    //      Progman
    //        └── WorkerW           (has SHELLDLL_DefView child)  <- icons
    //        └── WorkerW           (empty)                        <- our target
    //
    //  We look for a WorkerW that is a *sibling* of the WorkerW containing
    //  SHELLDLL_DefView, walking top-level windows in z-order.
    BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
    {
        EnumContext* ctx = reinterpret_cast<EnumContext*>(lParam);

        HWND shellView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
        if (shellView != nullptr)
        {
            // The WorkerW we want is the *next* top-level window after this one.
            HWND candidate = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
            if (candidate != nullptr)
            {
                ctx->foundWorkerW = candidate;
            }
        }

        return TRUE; // keep enumerating
    }
}

Desktop::~Desktop()
{
    Shutdown();
}

LRESULT CALLBACK Desktop::RenderHostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        // Prevent flicker; the video surface (or a child HWND owned by
        // libvlc) fully covers this window's client area during playback.
        return 1;

    case WM_DESTROY:
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

HWND Desktop::FindOrCreateWorkerW()
{
    m_progman = FindWindowW(L"Progman", nullptr);
    if (m_progman == nullptr)
    {
        Utilities::DebugLog(L"Progman window not found.");
        return nullptr;
    }

    // Ask Progman to spawn the WorkerW used for wallpaper hosting.
    // This is an undocumented but extremely well-known message (0x052C),
    // used by virtually every desktop-wallpaper utility since Windows Vista.
    DWORD_PTR result = 0;
    SendMessageTimeoutW(m_progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);

    HWND workerW = nullptr;
    EnumContext ctx;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    workerW = ctx.foundWorkerW;

    if (workerW == nullptr)
    {
        // Fallback path seen on some Windows 10/11 builds: WorkerW is
        // created as a direct child of the desktop with no sibling
        // relationship to SHELLDLL_DefView's parent. Search all top-level
        // WorkerW windows and pick the last (topmost in creation order) one
        // that currently has no children - that is almost always the blank
        // one reserved for wallpaper content.
        HWND candidate = nullptr;
        while ((candidate = FindWindowExW(nullptr, candidate, L"WorkerW", nullptr)) != nullptr)
        {
            if (GetWindow(candidate, GW_CHILD) == nullptr)
            {
                workerW = candidate;
            }
        }
    }

    return workerW;
}

bool Desktop::Initialize(HINSTANCE hInstance, int x, int y, int width, int height)
{
    m_hInstance = hInstance;

    if (!m_classRegistered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = RenderHostWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kRenderHostClassName;

        if (RegisterClassExW(&wc) == 0)
        {
            Utilities::DebugLog(L"Failed to register render host window class.");
            return false;
        }
        m_classRegistered = true;
    }

    m_workerW = FindOrCreateWorkerW();
    if (m_workerW == nullptr)
    {
        Utilities::DebugLog(L"Could not locate a suitable WorkerW window.");
        return false;
    }

    // WS_CHILD so it lives inside WorkerW and is clipped/composited with it.
    m_renderHost = CreateWindowExW(
        0,
        kRenderHostClassName,
        L"VideoWallpaperRenderHost",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, width, height,
        m_workerW,
        nullptr,
        hInstance,
        nullptr);

    if (m_renderHost == nullptr)
    {
        Utilities::DebugLog(L"Failed to create render host window.");
        return false;
    }

    ShowWindow(m_renderHost, SW_SHOW);
    UpdateWindow(m_renderHost);

    return true;
}

bool Desktop::ReattachToWorkerW()
{
    HWND newWorkerW = FindOrCreateWorkerW();
    if (newWorkerW == nullptr)
        return false;

    if (newWorkerW == m_workerW && IsWindow(m_renderHost))
        return true; // still valid, nothing to do

    m_workerW = newWorkerW;

    if (!IsWindow(m_renderHost))
    {
        // The old render host died along with the old explorer session;
        // caller (App) is responsible for recreating VideoPlayer's embed
        // target. We signal failure so App can call Initialize() again.
        return false;
    }

    SetParent(m_renderHost, m_workerW);
    return true;
}

void Desktop::Resize(int x, int y, int width, int height)
{
    if (m_renderHost != nullptr)
    {
        SetWindowPos(m_renderHost, nullptr, x, y, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void Desktop::Shutdown()
{
    if (m_renderHost != nullptr)
    {
        DestroyWindow(m_renderHost);
        m_renderHost = nullptr;
    }

    if (m_classRegistered && m_hInstance != nullptr)
    {
        UnregisterClassW(kRenderHostClassName, m_hInstance);
        m_classRegistered = false;
    }

    m_workerW = nullptr;
    m_progman = nullptr;
}
