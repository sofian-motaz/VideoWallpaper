// VideoPlayer.cpp
#include "VideoPlayer.h"
#include "Utilities.h"
#include <cstdio>

// Minimal mirror of the real libvlc_state_t enum (stable ABI since libvlc 2.x).
namespace LibVlcState
{
    constexpr int NothingSpecial = 0;
    constexpr int Opening = 1;
    constexpr int Buffering = 2;
    constexpr int Playing = 3;
    constexpr int Paused = 4;
    constexpr int Stopped = 5;
    constexpr int Ended = 6;
    constexpr int Error = 7;
}

VideoPlayer::~VideoPlayer()
{
    Shutdown();
}

template <typename T>
static bool ResolveProc(HMODULE mod, const char* name, T& outFn, std::wstring& outError)
{
    outFn = reinterpret_cast<T>(GetProcAddress(mod, name));
    if (outFn == nullptr)
    {
        outError = L"LibVLC is missing an expected function: " + Utilities::Utf8ToWide(name) +
                   L"\nYour installed VLC version may be too old or incompatible. "
                   L"Please install VLC 3.0 or newer.";
        return false;
    }
    return true;
}

bool VideoPlayer::LoadFunctionPointers(std::wstring& outError)
{
#define RESOLVE(fn) if (!ResolveProc(m_hLibVlc, #fn, p_##fn, outError)) return false;

    RESOLVE(libvlc_new);
    RESOLVE(libvlc_release);
    RESOLVE(libvlc_media_new_path);
    RESOLVE(libvlc_media_release);
    RESOLVE(libvlc_media_player_new_from_media);
    RESOLVE(libvlc_media_player_release);
    RESOLVE(libvlc_media_player_set_hwnd);
    RESOLVE(libvlc_media_player_play);
    RESOLVE(libvlc_media_player_pause);
    RESOLVE(libvlc_media_player_stop);
    RESOLVE(libvlc_media_player_is_playing);
    RESOLVE(libvlc_media_player_get_state);
    RESOLVE(libvlc_audio_set_mute);
    RESOLVE(libvlc_audio_get_mute);
    RESOLVE(libvlc_video_set_scale);
    RESOLVE(libvlc_video_set_aspect_ratio);
    RESOLVE(libvlc_video_get_size);
    RESOLVE(libvlc_errmsg);
    RESOLVE(libvlc_media_player_set_position);

#undef RESOLVE
    return true;
}

std::wstring VideoPlayer::LastLibVlcError() const
{
    if (p_libvlc_errmsg == nullptr)
        return L"Unknown LibVLC error.";

    const char* msg = p_libvlc_errmsg();
    if (msg == nullptr)
        return L"Unknown LibVLC error.";

    return Utilities::Utf8ToWide(std::string(msg));
}

bool VideoPlayer::Initialize(const std::wstring& vlcDirectory, std::wstring& outError)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // libvlccore.dll must be loadable first; libvlc.dll depends on it.
    std::wstring corePath = vlcDirectory + L"\\libvlccore.dll";
    std::wstring vlcPath = vlcDirectory + L"\\libvlc.dll";

    m_hLibVlcCore = LoadLibraryExW(corePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m_hLibVlcCore == nullptr)
    {
        outError = L"Failed to load libvlccore.dll from:\n" + corePath;
        return false;
    }

    m_hLibVlc = LoadLibraryExW(vlcPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m_hLibVlc == nullptr)
    {
        outError = L"Failed to load libvlc.dll from:\n" + vlcPath;
        FreeLibrary(m_hLibVlcCore);
        m_hLibVlcCore = nullptr;
        return false;
    }

    if (!LoadFunctionPointers(outError))
    {
        Shutdown();
        return false;
    }

    // Arguments tuned for a lightweight, headless-ish wallpaper renderer:
    // disable the video title overlay, skip plugin cache rebuild prompts,
    // and prefer hardware decoding when the platform supports it.
    const char* argv[] = {
        "--quiet",
        "--no-video-title-show",
        "--avcodec-hw=any",
        "--no-osd",
        "--no-stats",
        "--no-snapshot-preview",
        "--no-inhibit"
    };
    int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));

    m_libvlcInstance = p_libvlc_new(argc, argv);
    if (m_libvlcInstance == nullptr)
    {
        outError = L"libvlc_new() failed. " + LastLibVlcError();
        Shutdown();
        return false;
    }

    return true;
}

void VideoPlayer::SetRenderTarget(HWND hwndTarget)
{
    m_renderTarget = hwndTarget;
    if (m_mediaPlayer != nullptr && p_libvlc_media_player_set_hwnd != nullptr)
    {
        p_libvlc_media_player_set_hwnd(m_mediaPlayer, static_cast<void*>(hwndTarget));
    }
}

bool VideoPlayer::Open(const std::wstring& filePath, std::wstring& outError)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_libvlcInstance == nullptr)
    {
        outError = L"Playback engine is not initialized.";
        m_state = PlaybackState::Error;
        return false;
    }

    if (!Utilities::FileExists(filePath))
    {
        outError = L"The selected file could not be found:\n" + filePath;
        m_state = PlaybackState::Error;
        return false;
    }

    if (!Utilities::IsSupportedVideoFile(filePath))
    {
        outError = L"Unsupported file type. Supported formats: MP4, AVI, MKV, WMV, MOV.";
        m_state = PlaybackState::Error;
        return false;
    }

    // Tear down any previous media/player before opening a new file.
    if (m_mediaPlayer != nullptr)
    {
        p_libvlc_media_player_stop(m_mediaPlayer);
        p_libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    if (m_media != nullptr)
    {
        p_libvlc_media_release(m_media);
        m_media = nullptr;
    }

    std::string utf8Path = Utilities::WideToUtf8(filePath);
    m_media = p_libvlc_media_new_path(m_libvlcInstance, utf8Path.c_str());
    if (m_media == nullptr)
    {
        outError = L"Failed to create media from path. " + LastLibVlcError();
        m_state = PlaybackState::Error;
        return false;
    }

    m_mediaPlayer = p_libvlc_media_player_new_from_media(m_media);
    if (m_mediaPlayer == nullptr)
    {
        outError = L"Failed to create media player. " + LastLibVlcError();
        p_libvlc_media_release(m_media);
        m_media = nullptr;
        m_state = PlaybackState::Error;
        return false;
    }

    if (m_renderTarget != nullptr)
    {
        p_libvlc_media_player_set_hwnd(m_mediaPlayer, static_cast<void*>(m_renderTarget));
    }

    p_libvlc_audio_set_mute(m_mediaPlayer, m_muted ? 1 : 0);

    m_currentFilePath = filePath;
    ApplyScaleModeGeometry();

    return true;
}

void VideoPlayer::Play()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mediaPlayer == nullptr)
        return;

    if (p_libvlc_media_player_play(m_mediaPlayer) == 0)
    {
        m_state = PlaybackState::Playing;
    }
    else
    {
        m_state = PlaybackState::Error;
    }
}

void VideoPlayer::Pause()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mediaPlayer == nullptr)
        return;

    if (p_libvlc_media_player_is_playing(m_mediaPlayer))
    {
        p_libvlc_media_player_pause(m_mediaPlayer);
        m_state = PlaybackState::Paused;
    }
    else
    {
        // Resume from pause.
        p_libvlc_media_player_play(m_mediaPlayer);
        m_state = PlaybackState::Playing;
    }
}

void VideoPlayer::Stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mediaPlayer == nullptr)
        return;

    p_libvlc_media_player_stop(m_mediaPlayer);
    m_state = PlaybackState::Stopped;
}

void VideoPlayer::SetMuted(bool muted)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
    if (m_mediaPlayer != nullptr)
    {
        p_libvlc_audio_set_mute(m_mediaPlayer, muted ? 1 : 0);
    }
}

void VideoPlayer::SetScaleMode(ScaleMode mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scaleMode = mode;
    ApplyScaleModeGeometry();
}

void VideoPlayer::ApplyScaleModeGeometry()
{
    // Called with m_mutex already held by the caller.
    if (m_mediaPlayer == nullptr || p_libvlc_video_set_aspect_ratio == nullptr)
        return;

    switch (m_scaleMode)
    {
    case ScaleMode::Stretch:
        // No aspect ratio constraint + zero scale (=auto-fit window) results
        // in the video being stretched to exactly fill the render window,
        // distorting the original aspect ratio.
        p_libvlc_video_set_aspect_ratio(m_mediaPlayer, nullptr);
        p_libvlc_video_set_scale(m_mediaPlayer, 0.0f);
        break;

    case ScaleMode::Fit:
    case ScaleMode::Fill:
    default:
        // For both Fit and Fill we let libvlc auto-scale (0.0f) while
        // preserving the source aspect ratio (nullptr => derived from the
        // video's own sample aspect ratio). Fit therefore letterboxes
        // within the render window. True edge-to-edge "Fill" (cropping the
        // overflow) is approximated by the render host window itself being
        // slightly larger than the screen and centered - see App::UpdateScaleGeometry,
        // which resizes the host window based on the reported video size.
        p_libvlc_video_set_aspect_ratio(m_mediaPlayer, nullptr);
        p_libvlc_video_set_scale(m_mediaPlayer, 0.0f);
        break;
    }
}

void VideoPlayer::Tick()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_mediaPlayer == nullptr || p_libvlc_media_player_get_state == nullptr)
        return;

    int state = p_libvlc_media_player_get_state(m_mediaPlayer);

    if (state == LibVlcState::Ended || state == LibVlcState::Stopped)
    {
        if (m_loop && m_state == PlaybackState::Playing)
        {
            // Manual loop: reset to the start and resume playback. This is
            // more reliable across LibVLC versions/builds than relying
            // solely on the "input-repeat" media option.
            p_libvlc_media_player_stop(m_mediaPlayer);
            p_libvlc_media_player_set_position(m_mediaPlayer, 0.0f);
            p_libvlc_media_player_play(m_mediaPlayer);
        }
    }
    else if (state == LibVlcState::Error)
    {
        m_state = PlaybackState::Error;
    }
}

void VideoPlayer::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_mediaPlayer != nullptr)
    {
        if (p_libvlc_media_player_stop != nullptr)
            p_libvlc_media_player_stop(m_mediaPlayer);
        if (p_libvlc_media_player_release != nullptr)
            p_libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }

    if (m_media != nullptr)
    {
        if (p_libvlc_media_release != nullptr)
            p_libvlc_media_release(m_media);
        m_media = nullptr;
    }

    if (m_libvlcInstance != nullptr)
    {
        if (p_libvlc_release != nullptr)
            p_libvlc_release(m_libvlcInstance);
        m_libvlcInstance = nullptr;
    }

    if (m_hLibVlc != nullptr)
    {
        FreeLibrary(m_hLibVlc);
        m_hLibVlc = nullptr;
    }

    if (m_hLibVlcCore != nullptr)
    {
        FreeLibrary(m_hLibVlcCore);
        m_hLibVlcCore = nullptr;
    }

    m_state = PlaybackState::Stopped;
}
