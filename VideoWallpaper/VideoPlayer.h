// VideoPlayer.h
// Thin, RAII-friendly wrapper around LibVLC that:
//   - Loads libvlc.dll dynamically at runtime (no build-time dependency on
//     the VLC SDK import libs; only vlc's public C headers are needed).
//   - Embeds video output directly into a given HWND (the Desktop render
//     host window).
//   - Loops playback forever.
//   - Supports Fit / Fill / Stretch scaling modes.
//   - Mutes audio for wallpaper use.
//
// LibVLC function pointers are resolved with GetProcAddress so the app can
// start, detect a missing VLC installation, and show a friendly dialog
// instead of failing to launch with a missing-DLL error.

#pragma once

#include <Windows.h>
#include <string>
#include <functional>
#include <mutex>

// Forward declarations for the small subset of the LibVLC C API we use.
// These mirror the real libvlc struct/typedefs closely enough to link
// against the official libvlc.dll ABI without requiring the full VLC SDK
// headers to be present in the project.
struct libvlc_instance_t;
struct libvlc_media_t;
struct libvlc_media_player_t;
struct libvlc_event_manager_t;

enum class ScaleMode
{
    Fit,      // preserve aspect ratio, letterbox/pillarbox
    Fill,     // preserve aspect ratio, crop overflow to cover the screen
    Stretch   // ignore aspect ratio, distort to exactly fill the screen
};

enum class PlaybackState
{
    Stopped,
    Playing,
    Paused,
    Error
};

class VideoPlayer
{
public:
    VideoPlayer() = default;
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // Dynamically loads libvlc.dll/libvlccore.dll from vlcDirectory and
    // creates a libvlc_instance. Returns false (and fills outError) if the
    // DLLs can't be loaded or libvlc fails to initialize.
    bool Initialize(const std::wstring& vlcDirectory, std::wstring& outError);

    // Releases the media player, media, and libvlc instance, and unloads
    // the DLLs. Safe to call multiple times.
    void Shutdown();

    // Points libvlc's video output at hwndTarget. Must be called once after
    // Initialize() and before Play().
    void SetRenderTarget(HWND hwndTarget);

    // Opens filePath (UTF-16) and begins playback looping forever.
    // Returns false and fills outError on failure (bad path, unsupported
    // codec, corrupted file, etc).
    bool Open(const std::wstring& filePath, std::wstring& outError);

    void Play();
    void Pause();
    void Stop();

    void SetLooping(bool loop) { m_loop = loop; }
    bool IsLooping() const { return m_loop; }

    void SetMuted(bool muted);
    bool IsMuted() const { return m_muted; }

    void SetScaleMode(ScaleMode mode);
    ScaleMode GetScaleMode() const { return m_scaleMode; }

    PlaybackState GetState() const { return m_state; }

    const std::wstring& GetCurrentFilePath() const { return m_currentFilePath; }

    // Should be called periodically (e.g. every 500ms from a timer) so the
    // player can detect "end reached" and manually loop (more reliable
    // across libvlc versions than the input-repeat option alone), and so it
    // can reapply scale-mode geometry once the video's native size is known.
    void Tick();

    bool IsInitialized() const { return m_libvlcInstance != nullptr; }

private:
    // ---- Dynamically resolved LibVLC entry points ----
    using PFN_libvlc_new = libvlc_instance_t* (*)(int, const char* const*);
    using PFN_libvlc_release = void (*)(libvlc_instance_t*);
    using PFN_libvlc_media_new_path = libvlc_media_t* (*)(libvlc_instance_t*, const char*);
    using PFN_libvlc_media_release = void (*)(libvlc_media_t*);
    using PFN_libvlc_media_player_new_from_media = libvlc_media_player_t* (*)(libvlc_media_t*);
    using PFN_libvlc_media_player_release = void (*)(libvlc_media_player_t*);
    using PFN_libvlc_media_player_set_hwnd = void (*)(libvlc_media_player_t*, void*);
    using PFN_libvlc_media_player_play = int (*)(libvlc_media_player_t*);
    using PFN_libvlc_media_player_pause = void (*)(libvlc_media_player_t*);
    using PFN_libvlc_media_player_stop = void (*)(libvlc_media_player_t*);
    using PFN_libvlc_media_player_is_playing = int (*)(libvlc_media_player_t*);
    using PFN_libvlc_media_player_get_state = int (*)(libvlc_media_player_t*);
    using PFN_libvlc_audio_set_mute = void (*)(libvlc_media_player_t*, int);
    using PFN_libvlc_audio_get_mute = int (*)(libvlc_media_player_t*);
    using PFN_libvlc_video_set_scale = void (*)(libvlc_media_player_t*, float);
    using PFN_libvlc_video_set_aspect_ratio = void (*)(libvlc_media_player_t*, const char*);
    using PFN_libvlc_video_get_size = int (*)(libvlc_media_player_t*, unsigned, unsigned*, unsigned*);
    using PFN_libvlc_errmsg = const char* (*)();
    using PFN_libvlc_media_player_set_position = void (*)(libvlc_media_player_t*, float);

    PFN_libvlc_new p_libvlc_new = nullptr;
    PFN_libvlc_release p_libvlc_release = nullptr;
    PFN_libvlc_media_new_path p_libvlc_media_new_path = nullptr;
    PFN_libvlc_media_release p_libvlc_media_release = nullptr;
    PFN_libvlc_media_player_new_from_media p_libvlc_media_player_new_from_media = nullptr;
    PFN_libvlc_media_player_release p_libvlc_media_player_release = nullptr;
    PFN_libvlc_media_player_set_hwnd p_libvlc_media_player_set_hwnd = nullptr;
    PFN_libvlc_media_player_play p_libvlc_media_player_play = nullptr;
    PFN_libvlc_media_player_pause p_libvlc_media_player_pause = nullptr;
    PFN_libvlc_media_player_stop p_libvlc_media_player_stop = nullptr;
    PFN_libvlc_media_player_is_playing p_libvlc_media_player_is_playing = nullptr;
    PFN_libvlc_media_player_get_state p_libvlc_media_player_get_state = nullptr;
    PFN_libvlc_audio_set_mute p_libvlc_audio_set_mute = nullptr;
    PFN_libvlc_audio_get_mute p_libvlc_audio_get_mute = nullptr;
    PFN_libvlc_video_set_scale p_libvlc_video_set_scale = nullptr;
    PFN_libvlc_video_set_aspect_ratio p_libvlc_video_set_aspect_ratio = nullptr;
    PFN_libvlc_video_get_size p_libvlc_video_get_size = nullptr;
    PFN_libvlc_errmsg p_libvlc_errmsg = nullptr;
    PFN_libvlc_media_player_set_position p_libvlc_media_player_set_position = nullptr;

    bool LoadFunctionPointers(std::wstring& outError);
    void ApplyScaleModeGeometry();
    std::wstring LastLibVlcError() const;

    HMODULE m_hLibVlcCore = nullptr;
    HMODULE m_hLibVlc = nullptr;

    libvlc_instance_t* m_libvlcInstance = nullptr;
    libvlc_media_t* m_media = nullptr;
    libvlc_media_player_t* m_mediaPlayer = nullptr;

    HWND m_renderTarget = nullptr;
    std::wstring m_currentFilePath;

    bool m_loop = true;
    bool m_muted = true;
    ScaleMode m_scaleMode = ScaleMode::Fit;
    PlaybackState m_state = PlaybackState::Stopped;

    std::mutex m_mutex;
};
