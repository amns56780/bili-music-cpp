#include "media/VlcEngine.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cmath>
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
    void* (*libvlc_audio_output_device_enum)(void* player);
    void (*libvlc_audio_output_device_list_release)(void* list);
    int (*libvlc_audio_output_device_count)(void* instance, const char* module);
    char* (*libvlc_audio_output_device_longname)(void* instance, const char* module, int index);
    char* (*libvlc_audio_output_device_id)(void* instance, const char* module, int index);
    void (*libvlc_audio_output_device_set)(void* player, const char* module, const char* deviceId);
    void (*libvlc_free)(void* pointer);
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
    resolveSymbol(module, "libvlc_audio_output_device_enum", api->libvlc_audio_output_device_enum,
                  ok, error);
    resolveSymbol(module, "libvlc_audio_output_device_list_release",
                  api->libvlc_audio_output_device_list_release, ok, error);
    resolveSymbol(module, "libvlc_audio_output_device_count", api->libvlc_audio_output_device_count,
                  ok, error);
    resolveSymbol(module, "libvlc_audio_output_device_longname",
                  api->libvlc_audio_output_device_longname, ok, error);
    resolveSymbol(module, "libvlc_audio_output_device_id", api->libvlc_audio_output_device_id, ok,
                  error);
    resolveSymbol(module, "libvlc_audio_output_device_set", api->libvlc_audio_output_device_set, ok,
                  error);
    resolveSymbol(module, "libvlc_free", api->libvlc_free, ok, error);
    if (!ok) { delete api; return false; }

    // 和 Java 版一致：不做视频、安静启动、只播音频
    // 自检时可以用 BILIMUSIC_VLC_VERBOSE=1 打开 VLC 日志（排查暂停/恢复、aout 重建）
    std::vector<const char*> args = {"--no-video", "--quiet", "--intf=dummy", "--aout=mmdevice"};
    if (std::getenv("BILIMUSIC_VLC_VERBOSE") != nullptr) {
        args = {"--no-video", "--verbose=2", "--intf=dummy"};
    }
    instance_ = api->libvlc_new((int)args.size(), args.data());
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
    applyOutputDevice();
    if (api_->libvlc_media_player_play(player_) != 0) {
        appendError(error, "启动播放失败");
        return false;
    }
    // 新曲目：音频输出会重建，音量写出状态重置，pumpVolume 会重新下发一次
    pausedRequested_ = false;
    pausedAt_ = {};
    volumeApplied_ = false;
    lastVlcVolume_ = -1;
    volumeDirty_ = true;
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
    // 网络流缓存：默认 1000ms 在「暂停 → 恢复」时容易把缓冲耗干，恢复瞬间断断续续。
    // 加大到 5s 让恢复后先有足够的缓冲；http-reconnect 让连接断掉时自动重连。
    api_->libvlc_media_add_option(media_, ":network-caching=20000");
    api_->libvlc_media_add_option(media_, ":http-reconnect=true");
    // 网络音频时钟抖动相关：减少恢复播放后的断续/卡顿
    api_->libvlc_media_add_option(media_, ":clock-jitter=0");
    api_->libvlc_media_add_option(media_, ":clock-synchro=0");
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
    applyOutputDevice();
    if (api_->libvlc_media_player_play(player_) != 0) {
        appendError(error, "启动播放失败");
        return false;
    }
    pausedRequested_ = false;
    pausedAt_ = {};
    volumeApplied_ = false;
    lastVlcVolume_ = -1;
    volumeDirty_ = true;
    pumpVolume();
    return true;
}


std::vector<VlcEngine::AudioDevice> VlcEngine::audioOutputDevices() const {
    std::vector<AudioDevice> devices;
    if (!api_) return devices;
    // VLC 3 的设备列表挂在 player 上（mmdevice 模块在播放器创建后才会初始化）
    struct DeviceNode {
        DeviceNode* next;
        char* device;
        char* description;
    };
    if (player_ != nullptr && api_->libvlc_audio_output_device_enum != nullptr &&
        api_->libvlc_audio_output_device_list_release != nullptr) {
        auto* node = static_cast<DeviceNode*>(api_->libvlc_audio_output_device_enum(player_));
        for (auto* current = node; current != nullptr; current = current->next) {
            AudioDevice device;
            if (current->device != nullptr) device.id = current->device;
            if (current->description != nullptr) device.name = current->description;
            if (device.name.empty()) device.name = device.id;
            if (!device.id.empty()) devices.push_back(std::move(device));
        }
        if (node != nullptr) api_->libvlc_audio_output_device_list_release(node);
        if (!devices.empty()) return devices;
    }
    if (instance_ != nullptr && api_->libvlc_audio_output_device_count != nullptr) {
        auto enumerate = [&](const char* module) {
            devices.clear();
            const int count = api_->libvlc_audio_output_device_count(instance_, module);
            for (int i = 0; i < count; ++i) {
                char* longName = api_->libvlc_audio_output_device_longname(instance_, module, i);
                char* deviceId = api_->libvlc_audio_output_device_id(instance_, module, i);
                AudioDevice device;
                if (deviceId != nullptr) device.id = deviceId;
                if (longName != nullptr) device.name = longName;
                if (device.name.empty()) device.name = device.id;
                if (deviceId != nullptr) api_->libvlc_free(deviceId);
                if (longName != nullptr) api_->libvlc_free(longName);
                if (!device.id.empty()) devices.push_back(std::move(device));
            }
        };
        enumerate("mmdevice");
        if (devices.empty()) enumerate(nullptr);
    }
    return devices;
}

void VlcEngine::applyOutputDevice() {
    if (!api_ || !player_ || outputDeviceId_.empty()) return;
    api_->libvlc_audio_output_device_set(player_, "mmdevice", outputDeviceId_.c_str());
}

void VlcEngine::pause() {
    if (!api_ || !player_) return;
    pausedRequested_ = true;
    pausedAt_ = std::chrono::steady_clock::now();
    api_->libvlc_media_player_set_pause(player_, 1);
}

void VlcEngine::resume() {
    if (!api_ || !player_) return;
    // 用 libvlc_media_player_play 恢复：对已暂停的播放器等价于恢复播放，
    // 不会重建音频输出，实测比 set_pause(0) 的恢复静音比例略低。
    pausedRequested_ = false;
    pausedAt_ = {};
    api_->libvlc_media_player_play(player_);
}

void VlcEngine::resumeByPlay() {
    // libvlc_media_player_play 对已暂停的播放器等价于恢复播放，不重建 aout
    if (api_ && player_) api_->libvlc_media_player_play(player_);
}

void VlcEngine::togglePause() {
    if (!api_ || !player_) return;
    // 用自己记录的暂停状态，避免缓冲期间 isPlaying()==false 把「暂停」误判成「恢复」
    if (pausedRequested_) {
        resume();
    } else {
        pause();
    }
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
    volumeDirty_ = true;
    pumpVolume();
}

void VlcEngine::pumpVolume() {
    if (!api_ || !player_) return;

    const uint64_t now = GetTickCount64();
    if (!volumeDirty_ && volumeApplied_ && now - volumeCheckTick_ < 250) return;
    volumeCheckTick_ = now;

    if (sessionVolumeEnabled_) {
        // 只用 WASAPI 应用级音量；会话正在重建（换曲/恢复）时**不要**回退去写 VLC 音量，
        // 否则会在 100%（WASAPI）和 cbrt(80)≈43（VLC）之间来回跳，听感就是「一炸一炸」。
        if (!appVolume_.refresh() || !appVolume_.available()) return;
        const int current = appVolume_.volume();
        if (!volumeApplied_ || volumeDirty_ || current != volume_) {
            // 换曲后新的音频会话音量默认是 100%：这里必须强制写回用户音量，
            // 否则下一首会以系统默认音量播放，直到用户拖一下音量条才恢复。
            const bool reapplying = !volumeApplied_;
            if (!appVolume_.setVolume(volume_, /*force=*/true)) return;
            if (reapplying) {
                AppLog::info("AppVolume", "换曲后重新写入应用级音量 -> " +
                                              std::to_string(volume_) + "%");
            }
            if (lastVlcVolume_ != 100) {
                api_->libvlc_audio_set_volume(player_, 100);
                lastVlcVolume_ = 100;
            }
            volumeApplied_ = true;
            volumeDirty_ = false;
        } else {
            volumeDirty_ = false;
        }
        return;
    }

    // 回退到 VLC 时用和 Java 版一致的立方根曲线：
    //   VLC 的音量是立方响应，直接给百分比听感不对，反推 vlcVolume = 100 * cbrt(volume/100)
    int vlcVolume = (int)std::lround(100.0 * std::cbrt((double)volume_ / 100.0));
    vlcVolume = vlcVolume < 0 ? 0 : (vlcVolume > 100 ? 100 : vlcVolume);
    if (!volumeApplied_ || volumeDirty_ || lastVlcVolume_ != vlcVolume) {
        api_->libvlc_audio_set_volume(player_, vlcVolume);
        lastVlcVolume_ = vlcVolume;
        volumeApplied_ = true;
        volumeDirty_ = false;
    }
}
