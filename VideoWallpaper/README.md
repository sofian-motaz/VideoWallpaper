# Video Wallpaper

Plays MP4, AVI, MKV, WMV, or MOV video files as your live Windows desktop
wallpaper — behind your desktop icons, not in a media player window — using
the classic **WorkerW** technique and **LibVLC** for decoding.

Supported: Windows 7 SP1 (x86/x64), Windows 8/8.1, Windows 10, Windows 11.

---

## How it works

1. **WorkerW embedding** (`Desktop.cpp`): sends the undocumented `0x052C`
   message to the `Progman` window, which makes `explorer.exe` spawn a
   `WorkerW` window used to host wallpaper content. We locate it via
   `EnumWindows`/`FindWindowEx`, then `SetParent()` our own borderless
   render-host window into it. Windows composites our window below the
   desktop icons automatically — no icon hooking or owner-draw needed.
2. **Playback** (`VideoPlayer.cpp`): LibVLC is loaded dynamically at runtime
   (`LoadLibrary` + `GetProcAddress`, no VLC SDK required at *build* time).
   `libvlc_media_player_set_hwnd()` embeds the video surface directly into
   the render-host window. End-of-stream is detected on a timer and the
   media is manually rewound/replayed for a seamless loop.
3. **UI** (`App.cpp`): a small, ordinary Win32 control panel window (Open /
   Play / Pause / Stop / Loop / Autostart / Mute / Fit / Fill / Stretch)
   plus a system tray icon (`Tray.cpp`) with a right-click menu. Closing the
   control window (✕ or minimize) hides it to the tray; playback continues.

## Requirements to *run* the app

- **VLC media player must be installed** (free, https://www.videolan.org/vlc/),
  matching your app's bitness (64-bit VLC for the x64 build, 32-bit VLC for
  the Win32 build) — **or** copy `libvlc.dll`, `libvlccore.dll`, and the
  `plugins\` folder from an existing VLC install into a folder named
  `libvlc\` next to `VideoWallpaper.exe`. The app searches, in order:
  1. `.\libvlc\` next to the EXE (portable/vendored copy)
  2. The EXE's own directory
  3. The `HKLM/HKCU\SOFTWARE\VideoLAN\VLC` registry `InstallDir` key
  4. Common `Program Files` install paths
  5. Your `PATH` environment variable

  If none are found, a dialog explains exactly what to do — the app does
  **not** silently fail or crash.

## Building

### Option A — Visual Studio 2019 (or newer)

1. Open `VideoWallpaper.sln`.
2. Pick a configuration/platform (`Release|x64` recommended; `Win32` for
   32-bit Windows 7 machines).
3. Build (`Ctrl+Shift+B`). Output goes to `bin\<Platform>\<Configuration>\`.

No external SDK/NuGet dependency is required to *build* — LibVLC is loaded
dynamically at runtime, so the compiler never needs `libvlc.h` or an import
library.

### Option B — CMake (used by CI)

```powershell
cmake -B build -A x64        # or -A Win32 for 32-bit
cmake --build build --config Release
```

## Project layout

| File                     | Purpose                                                          |
|--------------------------|--------------------------------------------------------------------|
| `main.cpp`               | `wWinMain` entry point, single-instance guard                     |
| `App.h` / `App.cpp`      | Main control window, UI controls, orchestration                   |
| `Desktop.h` / `.cpp`     | WorkerW discovery/creation, render-host window                    |
| `VideoPlayer.h` / `.cpp` | Dynamic LibVLC loading, playback, looping, scale modes             |
| `Tray.h` / `.cpp`        | System tray icon + right-click context menu                       |
| `Utilities.h` / `.cpp`   | Error dialogs, LibVLC discovery, file dialogs, string/DPI helpers  |
| `resource.rc/.h`         | App/tray icons, version info                                       |
| `app.manifest`           | DPI awareness, Common Controls v6, OS compatibility declarations  |
| `CMakeLists.txt`         | Alternate/CI build path                                            |
| `.github/workflows/build.yml` | CI: builds Win32 + x64 Release, publishes EXEs on version tags |

## Scale modes

- **Fit** — preserves aspect ratio, letterboxes/pillarboxes to avoid cropping.
- **Fill** — preserves aspect ratio, crops overflow so the video covers the
  entire screen with no bars.
- **Stretch** — ignores aspect ratio, distorts the video to exactly fill the
  screen.

## Known limitations

- If `explorer.exe` restarts (crash, "Restart Explorer" from Task Manager,
  certain shell extensions), a watchdog timer detects the lost `WorkerW`
  parent and re-attaches automatically within ~5 seconds.
- Multi-monitor setups: the render host spans the full virtual screen
  (`SM_XVIRTUALSCREEN`/`SM_CXVIRTUALSCREEN`), so the same video is
  stretched/fitted across all monitors combined rather than mirrored
  per-monitor. Per-monitor independent video is a natural follow-up but is
  out of scope for this initial version.
- This project has **not been build-verified in this delivery** (it was
  authored outside of a Windows/MSVC/LibVLC environment). Please do an
  initial local build (or check the GitHub Actions run) before relying on
  it, and file/fix any toolchain-specific compile nits you hit — the CI
  workflow (`.github/workflows/build.yml`) will do this automatically once
  pushed to a repository.

## License

Provided as-is, no warranty. LibVLC is licensed under LGPLv2.1+; if you
redistribute VLC's DLLs alongside this app, keep VLC's own license text
with them.
