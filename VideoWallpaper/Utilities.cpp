// Utilities.cpp
#include "Utilities.h"
#include <ShlObj.h>
#include <Shlwapi.h>
#include <CommDlg.h>
#include <algorithm>
#include <array>
#include <cwctype>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Advapi32.lib")

namespace Utilities
{
    void ShowErrorDialog(HWND hOwner, const std::wstring& title, const std::wstring& message)
    {
        MessageBoxW(hOwner, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }

    void ShowInfoDialog(HWND hOwner, const std::wstring& title, const std::wstring& message)
    {
        MessageBoxW(hOwner, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    }

    static std::wstring ToLowerCopy(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    }

    bool IsSupportedVideoFile(const std::wstring& filePath)
    {
        static const std::array<std::wstring, 5> kExtensions = { L".mp4", L".avi", L".mkv", L".wmv", L".mov" };

        std::wstring lower = ToLowerCopy(filePath);
        size_t dot = lower.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return false;

        std::wstring ext = lower.substr(dot);
        return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
    }

    bool FileExists(const std::wstring& filePath)
    {
        DWORD attrib = GetFileAttributesW(filePath.c_str());
        return (attrib != INVALID_FILE_ATTRIBUTES) && !(attrib & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::wstring GetExecutableDirectory()
    {
        wchar_t buffer[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len == 0 || len == MAX_PATH)
            return L".";

        std::wstring path(buffer, len);
        size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L".";

        return path.substr(0, slash);
    }

    static bool DirectoryHasLibVlc(const std::wstring& dir)
    {
        std::wstring dllPath = dir + L"\\libvlc.dll";
        std::wstring corePath = dir + L"\\libvlccore.dll";
        return FileExists(dllPath) && FileExists(corePath);
    }

    static std::optional<std::wstring> QueryVlcRegistryInstallDir()
    {
        const std::array<HKEY, 2> roots = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
        const std::array<std::wstring, 2> subkeys = {
            L"SOFTWARE\\VideoLAN\\VLC",
            L"SOFTWARE\\WOW6432Node\\VideoLAN\\VLC"
        };

        for (HKEY root : roots)
        {
            for (const auto& subkey : subkeys)
            {
                HKEY hKey;
                if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
                {
                    wchar_t buffer[MAX_PATH] = {};
                    DWORD size = sizeof(buffer);
                    DWORD type = 0;
                    LONG result = RegQueryValueExW(hKey, L"InstallDir", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
                    RegCloseKey(hKey);

                    if (result == ERROR_SUCCESS && type == REG_SZ)
                    {
                        std::wstring dir(buffer);
                        if (DirectoryHasLibVlc(dir))
                            return dir;
                    }
                }
            }
        }

        return std::nullopt;
    }

    std::optional<std::wstring> FindLibVlcDirectory()
    {
        std::wstring exeDir = GetExecutableDirectory();

        // 1. Portable vendored copy: .\libvlc (subfolder next to the exe)
        std::wstring vendored = exeDir + L"\\libvlc";
        if (DirectoryHasLibVlc(vendored))
            return vendored;

        // 2. Next to the executable
        if (DirectoryHasLibVlc(exeDir))
            return exeDir;

        // 3. Registry
        if (auto regDir = QueryVlcRegistryInstallDir())
            return regDir;

        // 4. Common install paths
        std::array<std::wstring, 4> commonPaths = {
            L"C:\\Program Files\\VideoLAN\\VLC",
            L"C:\\Program Files (x86)\\VideoLAN\\VLC",
            L"C:\\Program Files\\VLC",
            L"C:\\Program Files (x86)\\VLC"
        };
        for (const auto& p : commonPaths)
        {
            if (DirectoryHasLibVlc(p))
                return p;
        }

        // 5. Search PATH
        wchar_t pathEnv[32768] = {};
        DWORD len = GetEnvironmentVariableW(L"PATH", pathEnv, 32768);
        if (len > 0 && len < 32768)
        {
            std::wstring pathStr(pathEnv);
            size_t start = 0;
            while (start <= pathStr.size())
            {
                size_t sep = pathStr.find(L';', start);
                std::wstring entry = (sep == std::wstring::npos) ? pathStr.substr(start) : pathStr.substr(start, sep - start);
                if (!entry.empty() && DirectoryHasLibVlc(entry))
                    return entry;
                if (sep == std::wstring::npos)
                    break;
                start = sep + 1;
            }
        }

        return std::nullopt;
    }

    void PrependToDllSearchPath(const std::wstring& dir)
    {
        // SetDllDirectory affects LoadLibrary's search order for the process.
        SetDllDirectoryW(dir.c_str());

        // Also prepend to PATH in case libvlc.dll pulls in plugins that rely on it.
        wchar_t currentPath[32768] = {};
        GetEnvironmentVariableW(L"PATH", currentPath, 32768);
        std::wstring newPath = dir + L";" + currentPath;
        SetEnvironmentVariableW(L"PATH", newPath.c_str());

        // Point VLC_PLUGIN_PATH at the plugins subfolder if present, so libvlc
        // can find its codec/demux plugins regardless of current directory.
        std::wstring pluginsDir = dir + L"\\plugins";
        if (GetFileAttributesW(pluginsDir.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            SetEnvironmentVariableW(L"VLC_PLUGIN_PATH", pluginsDir.c_str());
        }
    }

    SIZE GetVirtualScreenSize()
    {
        SIZE s;
        s.cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        s.cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return s;
    }

    SIZE GetPrimaryScreenSize()
    {
        SIZE s;
        s.cx = GetSystemMetrics(SM_CXSCREEN);
        s.cy = GetSystemMetrics(SM_CYSCREEN);
        return s;
    }

    std::wstring Utf8ToWide(const std::string& utf8)
    {
        if (utf8.empty())
            return L"";

        int required = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring result(required, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), required);
        return result;
    }

    std::string WideToUtf8(const std::wstring& wide)
    {
        if (wide.empty())
            return "";

        int required = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string result(required, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), required, nullptr, nullptr);
        return result;
    }

    std::wstring OpenVideoFileDialog(HWND hOwner)
    {
        wchar_t fileBuffer[MAX_PATH] = {};

        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hOwner;
        ofn.lpstrFilter =
            L"Video Files (*.mp4;*.avi;*.mkv;*.wmv;*.mov)\0*.mp4;*.avi;*.mkv;*.wmv;*.mov\0"
            L"All Files (*.*)\0*.*\0\0";
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        ofn.lpstrTitle = L"Select a video for your wallpaper";

        if (GetOpenFileNameW(&ofn))
            return std::wstring(fileBuffer);

        return L"";
    }

    void DebugLog(const std::wstring& message)
    {
#ifndef NDEBUG
        std::wstring line = L"[VideoWallpaper] " + message + L"\n";
        OutputDebugStringW(line.c_str());
#else
        UNREFERENCED_PARAMETER(message);
#endif
    }
}
