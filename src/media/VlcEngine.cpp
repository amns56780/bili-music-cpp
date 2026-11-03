#include "media/VlcEngine.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

#include "util/AppLog.h"

namespace {

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring parentDir(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return path.substr(0, pos);
}

std::wstring exeDir() {
    wchar_t buffer[MAX_PATH * 4] = {};
    DWORD len = GetModuleFileNameW(nullptr, buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])));
    if (len == 0) return L".";
    return parentDir(std::wstring(buffer, len));
}

bool fileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void appendError(std::string* error, const std::string& text) {
    if (!error) return;
    if (!error->empty()) *error += "\n";
    *error += text;
}

template <typename T>
void resolveSymbol(HMODULE module, const char* name, T& target, bool& ok, std::string* error) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!target) {
        ok = false;
        appendError(error, std::string("libvlc 缺少导出函数：") + name);
    }
}

}  // namespace

struct VlcEngine::Api {
    void* (*libvlc_new)(int argc, const char* const* argv);
    void (*libvlc_release)(void* instance);
    const char* (*libvlc_get_version)();
    void* (*libvlc_media_new_path)(void* instance, const char* path);
    void* (*libvlc_media_new_location)(void* instance, const char* path);
    void (*libvlc_media_add_option)(void* media, const char* options);
    void (*libvlc_media_release)(void* media);
    void* (*libvlc_media_player_new_from_media)(void* media);
    void (*libvlc_media_player_release)(void* player);
    int (*libvlc_media_player_play)(void* player);
    void (*libvlc_media_player_set_pause)(void* player, int doPause);
    void (*libvlc_media_player_stop)(void* player);
    int (*libvlc_media_player_is_playing)(void* player);
    int64_t (*libvlc_media_player_get_time)(void* player);
    int64_t (*libvlc_media_player_get_length)(void* player);
    void (*libvlc_media_player_set_time)(void* player, int64_t time);
    int (*libvlc_audio_set_volume)(void* player, int volume);
    int (*libvlc_audio_get_volume)(void* player);
};

VlcEngine::VlcEngine() = default;

VlcEngine::~VlcEngine() { shutdown(); }

bool VlcEngine::init(std::string* error) {
    if (instance_) return true;

    std::vector<std::wstring> candidates;
    wchar_t envBuffer[32768] = {};
    DWORD envLen = GetEnvironmentVariableW(L"BILIMUSIC_VLC_DIR", envBuffer, (DWORD)(sizeof(envBuffer) / sizeof(wchar_t)));
    if (envLen > 0) candidates.push_back(std::wstring(envBuffer) + L"\\libvlc.dll");
    const std::wstring exe = exeDir();
    candidates.push_back(exe + L"\\vlc\\libvlc.dll");
    candidates.push_back(exe + L"\\libvlc.dll");
    candidates.push_back(L"C:\\Program Files\\VideoLAN\\VLC\\libvlc.dll");
    candidates.push_back(L"C:\\Program Files (x86)\\VideoLAN\\VLC\\libvlc.dll");

    std::wstring dllPath;
    for (const auto& candidate : candidates) {
        if (fileExists(candidate)) {
            dllPath = candidate;
            break;
        }
    }
    if (dllPath.empty()) {
        appendError(error, "找不到 libvlc.dll：依次尝试过下面这些位置");
        for (const auto& candidate : candidates) appendError(error, "  " + wideToUtf8(candidate));
        return false;
    }

    libraryDir_ = parentDir(dllPath);
    // 让 libvlc 能找到同目录的 libvlccore.dll 与 plugins/
    SetDllDirectoryW(libraryDir_.c_str());
    SetEnvironmentVariableW(L"VLC_PLUGIN_PATH", (libraryDir_ + L"\\plugins").c_str());

    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (!module) {
        appendError(error, "LoadLibrary 失败（" + wideToUtf8(dllPath) + "），错误码 " + std::to_string(GetLastError()));
        return false;
    }
    library_ = module;

    auto* api = new Api{};
    bool ok = true;
    resolveSymbol(module, "libvlc_new", api->libvlc_new, ok, error);
    resolveSymbol(module, "libvlc_release", api->libvlc_release, ok, error);
    resolveSymbol(module, "libvlc_get_version", api->libvlc_get_version, ok, error);
    resolveSymbol(module, "libvlc_media_new_path", api->libvlc_media_new_path, ok, error);
    resolveSymbol(module, "libvlc_media_new_location", api->libvlc_media_new_location, ok, error);
    resolveSymbol(module, "libvlc_media_add_option", api->libvlc_media_add_option, ok, error);
    resolveSymbol(module, "libvlc_media_release", api->libvlc_media_release, ok, error);
    resolveSymbol(module, "libvlc_media_player_new_from_media", api->libvlc_media_player_new_from_media, ok, error);
    resolveSymbol(module, "libvlc_media_player_release", api->libvlc_media_player_release, ok, error);
    resolveSymbol(module, "libvlc_media_player_play", api->libvlc_media_player_play, ok, error);
    resolveSymbol(module, "libvlc_media_player_set_pause", api->libvlc_media_player_set_pause, ok, error);
    resolveSymbol(module, "libvlc_media_player_stop", api->libvlc_media_player_stop, ok, error);
    resolveSymbol(module, "libvlc_media_player_is_playing", api->libvlc_media_player_is_playing, ok, error);
    resolveSymbol(module, "libvlc_media_player_get_time", api->libvlc_media_player_get_time, ok, error);
    resolveSymbol(module, "libvlc_media_player_get_length", api->libvlc_media_player_get_length, ok, error);
    resolveSymbol(module, "libvlc_media_player_set_time", api->libvlc_media_player_set_time, ok, error);
    resolveSymbol(module, "libvlc_audio_set_volume", api->libvlc_audio_set_volume, ok, error);
    resolveSymbol(module, "libvlc_audio_get_volume", api->libvlc_audio_get_volume, ok, error);
    if (!ok) { delete api; return false; }

    // 和 Java 版一致：不做视频、安静启动、只播音频
    const char* args[] = {"--no-video", "--quiet", "--intf=dummy"};
    instance_ = api->libvlc_new(3, args);
    if (!instance_) {
        delete api;
        appendError(error, "libvlc_new 失败（VLC 内核无法初始化）");
        return false;
    }
    api_ = api;

    // 优先使用 WASAPI 应用级音量：不经过 VLC，拖动音量条立即生效
    if (appVolume_.init()) {
        sessionVolumeEnabled_ = true;
        AppLog::info("VlcEngine", "音量走 WASAPI 应用级音量（不经过 VLC）");
    } else {
        sessionVolumeEnabled_ = false;
        AppLog::info("VlcEngine", "WASAPI 不可用，音量回退到 libvlc");
    }
    return true;
}

void VlcEngine::releasePlayer() {
    if (!api_) return;
    if (player_) {
        api_->libvlc_media_player_stop(player_);
        api_->libvlc_media_player_release(player_);
        player_ = nullptr;
    }
    if (media_) {
        api_->libvlc_media_release(media_);
        media_ = nullptr;
    }
}

void VlcEngine::shutdown() {
    releasePlayer();
    appVolume_.shutdown();
    if (api_ && instance_) api_->libvlc_release(instance_);
    instance_ = nullptr;
    delete api_;
    api_ = nullptr;
    if (library_) {
        FreeLibrary((HMODULE)library_);
        library_ = nullptr;
    }
}

std::string VlcEngine::version() const {
    if (!api_ || !api_->libvlc_get_version) return "unknown";
    const char* text = api_->libvlc_get_version();
    return text ? text : "unknown";
}

bool VlcEngine::playPath(const std::wstring& path, std::string* error) {
    if (!ready()) {
        appendError(error, "播放内核未初始化");
        return false;
    }
    releasePlayer();
    media_ = api_->libvlc_media_new_path(instance_, wideToUtf8(path).c_str());
    if (!media_) {
        appendError(error, "无法打开文件：" + wideToUtf8(path));
        return false;
    }
    player_ = api_->libvlc_media_player_new_from_media(media_);
    if (!player_) {
        appendError(error, "创建播放器失败");
        return false;
    }
    if (api_->libvlc_media_player_play(player_) != 0) {
        appendError(error, "启动播放失败");
        return false;
    }
    // 应用级音量在会话创建后才会生效，pumpVolume 会持续重试，这里先下发一次
    pumpVolume();
    return true;
}

bool VlcEngine::playUrl(const std::string& url, const std::string& referrer,
                        const std::string& userAgent, std::string* error) {
    if (!ready()) {
        appendError(error, "播放内核未初始化");
        return false;
    }
    releasePlayer();
    media_ = api_->libvlc_media_new_location(instance_, url.c_str());
    if (!media_) {
        appendError(error, "无法打开网络地址");
        return false;
    }
    if (!referrer.empty()) {
        api_->libvlc_media_add_option(media_, (":" + std::string("http-referrer=") + referrer).c_str());
    }
    if (!userAgent.empty()) {
        api_->libvlc_media_add_option(media_, (":" + std::string("http-user-agent=") + userAgent).c_str());
    }
    player_ = api_->libvlc_media_player_new_from_media(media_);
    if (!player_) {
        appendError(error, "创建播放器失败");
        return false;
    }
    if (api_->libvlc_media_player_play(player_) != 0) {
        appendError(error, "启动播放失败");
        return false;
    }
    pumpVolume();
    return true;
}

void VlcEngine::pause() {
    if (api_ && player_) api_->libvlc_media_player_set_pause(player_, 1);
}

void VlcEngine::resume() {
    if (api_ && player_) api_->libvlc_media_player_set_pause(player_, 0);
}

void VlcEngine::togglePause() {
    if (!api_ || !player_) return;
    if (isPlaying()) pause(); else resume();
}

void VlcEngine::stop() {
    if (api_ && player_) api_->libvlc_media_player_stop(player_);
}

bool VlcEngine::isPlaying() const {
    return api_ && player_ && api_->libvlc_media_player_is_playing(player_) != 0;
}

int64_t VlcEngine::positionMs() const {
    if (!api_ || !player_) return 0;
    return api_->libvlc_media_player_get_time(player_);
}

int64_t VlcEngine::lengthMs() const {
    if (!api_ || !player_) return 0;
    return api_->libvlc_media_player_get_length(player_);
}

void VlcEngine::seekMs(int64_t ms) {
    if (api_ && player_) api_->libvlc_media_player_set_time(player_, ms);
}

void VlcEngine::setVolume(int value) {
    volume_ = value < 0 ? 0 : (value > 100 ? 100 : value);
    pumpVolume();
}

void VlcEngine::pumpVolume() {
    if (!api_ || !player_) return;
    if (sessionVolumeEnabled_ && appVolume_.setVolume(volume_, /*force=*/true)) {
        // 应用级音量接管后，libvlc 固定 100%，避免双重衰减
        api_->libvlc_audio_set_volume(player_, 100);
        return;
    }
    api_->libvlc_audio_set_volume(player_, volume_);
}
