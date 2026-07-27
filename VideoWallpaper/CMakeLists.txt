# CMakeLists.txt
# Alternate build path to the .sln/.vcxproj, primarily used by the GitHub
# Actions CI workflow (.github/workflows/build.yml) so the project can be
# built headlessly with `cmake` + MSVC without opening Visual Studio.
#
# Usage (from a "Developer PowerShell for VS" prompt):
#   cmake -B build -A Win32          (or -A x64)
#   cmake --build build --config Release

cmake_minimum_required(VERSION 3.20)
project(VideoWallpaper LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "VideoWallpaper only builds for Windows (Win32 API + LibVLC).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WIN32_EXECUTABLE ON) # WinMain subsystem, not console

# Target Windows 7 SP1 as the minimum supported OS.
add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601 UNICODE _UNICODE)

if(MSVC)
    # Static CRT so the produced EXE has minimal external runtime dependencies.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /permissive-)
endif()

add_executable(VideoWallpaper
    main.cpp
    App.cpp
    Desktop.cpp
    VideoPlayer.cpp
    Tray.cpp
    Utilities.cpp
    resource.rc
)

target_include_directories(VideoWallpaper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(VideoWallpaper PRIVATE
    Shell32.lib
    Shlwapi.lib
    Comctl32.lib
    Comdlg32.lib
    Advapi32.lib
    User32.lib
    Gdi32.lib
)

# Embed app.manifest (DPI awareness / Common Controls v6 / OS compatibility).
if(MSVC)
    target_link_options(VideoWallpaper PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    )
endif()

set_target_properties(VideoWallpaper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin/Debug"
)

# If a vendored "libvlc" folder (libvlc.dll, libvlccore.dll, plugins/) is
# present next to this file, copy it alongside the built EXE so the app is
# self-contained without requiring a separate VLC install.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libvlc")
    add_custom_command(TARGET VideoWallpaper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/libvlc"
                "$<TARGET_FILE_DIR:VideoWallpaper>/libvlc"
        COMMENT "Copying vendored LibVLC redistributables next to the EXE"
    )
endif()
