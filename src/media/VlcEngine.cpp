#include "media/VlcEngine.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>

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

// WASAPI 枚举：VLC 3 的设备枚举依赖 player 且有时返回空（未起播 / mmdevice 未初始化），
// 这里用 Windows MMDevice API 兜底，保证设置弹窗始终能列出系统输出设备。
// MinGW 的 libuuid 里没有这两个 GUID 的定义，这里本地定义（值来自 mmdeviceapi.h）
const CLSID kClsidMMDeviceEnumerator = {
    0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
const IID kIidIMMDeviceEnumerator = {
    0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};

const PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::vector<std::pair<std::string, std::string>> enumerateWasapiRenderDevices() {
    std::vector<std::pair<std::string, std::string>> result;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return result;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                kIidIMMDeviceEnumerator,
                                reinterpret_cast<void**>(&enumerator))) ||
        enumerator == nullptr) {
        return result;
    }
    IMMDeviceCollection* collection = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) &&
        collection != nullptr) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(i, &device)) || device == nullptr) continue;
            std::string id;
            LPWSTR endpointId = nullptr;
            if (SUCCEEDED(device->GetId(&endpointId)) && endpointId != nullptr) {
                id = wideToUtf8(endpointId);
                CoTaskMemFree(endpointId);
            }
            std::string name;
            IPropertyStore* store = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store != nullptr) {
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(store->GetValue(kPkeyDeviceFriendlyName, &value)) &&
                    value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
                    name = wideToUtf8(value.pwszVal);
                }
                if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
                    CoTaskMemFree(value.pwszVal);
                }
                store->Release();
            }
            device->Release();
            if (!id.empty()) result.emplace_back(id, name.empty() ? id : name);
        }
        collection->Release();
    }
    enumerator->Release();
    return result;
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
    int (*libvlc_media_player_get_state)(void* player);
    int64_t (*libvlc_media_player_get_time)(void* player);
    int64_t (*libvlc_media_player_get_length)(void* player);
    int (*libvlc_media_player_set_time)(void* player, int64_t time);
    int (*libvlc_audio_set_volume)(void* player, int volume);
    int (*libvlc_audio_get_volume)(void* player);
    void* (*libvlc_audio_output_device_enum)(void* player);
    void (*libvlc_audio_output_device_list_release)(void* list);
    int (*libvlc_audio_output_device_count)(void* instance, const char* module);
    char* (*libvlc_audio_output_device_longname)(void* instance, const char* module, int index);
    char* (*libvlc_audio_output_device_id)(void* instance, const char* module, int index);
    void (*libvlc_audio_output_device_set)(void* player, const char* module, const char* deviceId);
    // 可选符号：强制指定 aout 模块 / 回读当前设备（用于确认切换是否真的生效）
    int (*libvlc_audio_output_set)(void* player, const char* name);
    char* (*libvlc_audio_output_device_get)(void* player);
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
    resolveSymbol(module, "libvlc_media_player_get_state", api->libvlc_media_player_get_state, ok, error);
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
    // 这两个在 VLC 3.0 都有，但个别精简构建可能没有：单独取，拿不到也不影响启动
    api->libvlc_audio_output_set =
        (int (*)(void*, const char*))GetProcAddress((HMODULE)module, "libvlc_audio_output_set");
    api->libvlc_audio_output_device_get =
        (char* (*)(void*))GetProcAddress((HMODULE)module, "libvlc_audio_output_device_get");
    if (!ok) { delete api; return false; }

    api_ = api;
    if (!createInstance(error)) {
        delete api;
        return false;
    }
    AppLog::info("VlcEngine", "VLC " + version() + "，内核目录 " + wideToUtf8(libraryDir_));

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

void VlcEngine::applyDeviceMediaOptions() {
    // VLC 3 的 aout 只在自己被创建时读设备；而 aout 建在 input 之下，
    // 会继承媒体项上的选项 —— 这是唯一能在"每次起播"都稳定生效的地方。
    if (media_ == nullptr || api_ == nullptr) return;
    if (outputDeviceId_.empty()) return;
    api_->libvlc_media_add_option(media_, ":aout=mmdevice");
    api_->libvlc_media_add_option(media_,
                                  (":" + std::string("mmdevice-audio-device=") + outputDeviceId_).c_str());
}

bool VlcEngine::createInstance(std::string* error) {
    // 和 Java 版一致：不做视频、安静启动、只播音频
    // 自检时可以用 BILIMUSIC_VLC_VERBOSE=1 打开 VLC 日志（排查暂停/恢复、aout 重建）
    std::vector<std::string> argStore;
    const bool verbose = std::getenv("BILIMUSIC_VLC_VERBOSE") != nullptr;
    argStore.push_back("--no-video");
    argStore.push_back(verbose ? "--verbose=2" : "--quiet");
    argStore.push_back("--intf=dummy");
    argStore.push_back("--aout=mmdevice");
    if (!outputDeviceId_.empty()) {
        // 这一条必须落在实例级：aout 创建时直接读它，不依赖"起播前设设备"的时机
        argStore.push_back("--mmdevice-audio-device=" + outputDeviceId_);
    }
    std::vector<const char*> args;
    args.reserve(argStore.size());
    for (const std::string& item : argStore) args.push_back(item.c_str());
    instance_ = api_->libvlc_new((int)args.size(), args.data());
    if (instance_ == nullptr) {
        if (error != nullptr) *error = "libvlc_new 失败（VLC 内核无法初始化）";
        return false;
    }
    return true;
}

bool VlcEngine::switchAudioDevice(const std::string& id) {
    if (api_ == nullptr) return false;
    releasePlayer();
    if (instance_ != nullptr) {
        api_->libvlc_release(instance_);
        instance_ = nullptr;
    }
    outputDeviceId_ = id;
    confirmedDeviceId_.clear();
    deviceVerifyLogged_ = false;
    deviceVerifyTick_ = 0;
    deviceWatchUntil_ = GetTickCount64() + 12000;
    deviceWatchNextTick_ = 0;
    deviceWatchLast_.clear();
    std::string error;
    if (!createInstance(&error)) {
        AppLog::warn("VlcEngine", "按设备重建 VLC 实例失败：" + error);
        return false;
    }
    AppLog::info("VlcEngine", "已按设备重建 VLC 实例：" +
                                  (id.empty() ? std::string("系统默认设备") : id));
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
    applyDeviceMediaOptions();
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
    appVolume_.resetRetryThrottle();
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
    // 网络流缓存：可配置（设置里选 2/5/10/20 秒）；越大抗卡顿越好，但 seek 后重新出声越慢。
    const std::string cacheOption = ":network-caching=" + std::to_string(networkCacheMs_);
    api_->libvlc_media_add_option(media_, cacheOption.c_str());
    api_->libvlc_media_add_option(media_, ":http-reconnect=true");
    // 网络音频时钟抖动相关：减少恢复播放后的断续/卡顿
    api_->libvlc_media_add_option(media_, ":clock-jitter=0");
    api_->libvlc_media_add_option(media_, ":clock-synchro=0");
    if (!referrer.empty()) {
        api_->libvlc_media_add_option(media_, (":" + std::string("http-referrer=") + referrer).c_str());
    }
    if (!userAgent.empty()) {
        api_->libvlc_media_add_option(media_, (":" + std::string("http-user-agent=") + userAgent).c_str());
    applyDeviceMediaOptions();
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
    appVolume_.resetRetryThrottle();
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
    if (devices.empty()) {
        for (auto& entry : enumerateWasapiRenderDevices()) {
            AudioDevice device;
            device.id = std::move(entry.first);
            device.name = std::move(entry.second);
            devices.push_back(std::move(device));
        }
    }
    return devices;
}

void VlcEngine::applyOutputDevice() {
    if (!api_ || !player_ || outputDeviceId_.empty()) return;
    // 先把这个 player 的 aout 模块钉死成 mmdevice，再指定设备：
    // 只调 device_set 时，如果当前 aout 不是 mmdevice（或还没建），VLC 会静默忽略。
    if (api_->libvlc_audio_output_set != nullptr) {
        api_->libvlc_audio_output_set(player_, "mmdevice");
    }
    api_->libvlc_audio_output_device_set(player_, "mmdevice", outputDeviceId_.c_str());
    if (confirmedDeviceId_ == outputDeviceId_) return;  // 这台设备已经确认生效，不必每首重新核对
    deviceVerifyTick_ = GetTickCount64() + 2500;  // 起播后 2.5s 回读（aout 建好、真的出声之后）
    deviceVerifyLogged_ = false;
    deviceVerifyAttempts_ = 0;
}

void VlcEngine::verifyOutputDevice() {
    // 切换设备后持续观察 12 秒：aout 重建需要一点时间，早期的采样可能还停在旧设备上。
    // 只有最终稳定落在目标设备才算真的生效。
    if (deviceWatchUntil_ != 0 && GetTickCount64() < deviceWatchUntil_) {
        if (GetTickCount64() >= deviceWatchNextTick_) {
            deviceWatchNextTick_ = GetTickCount64() + 1000;
            const std::string actual = appVolume_.findSessionDeviceId();
            if (!actual.empty() && actual != deviceWatchLast_) {
                deviceWatchLast_ = actual;
                AppLog::info("VlcEngine", "设备观察：会话现在挂 " + actual +
                                              (actual == outputDeviceId_ ? "（目标）" : "（还没切过去）"));
            }
        }
    }
    // VLC 3.0 没有「回读当前设备」的 API（libvlc_audio_output_device_get 是 4.0 的），
    // 所以这里改成看 WASAPI：本进程的音频会话实际挂在哪个 render endpoint 上。
    if (deviceVerifyLogged_) return;
    if (deviceVerifyTick_ == 0 || GetTickCount64() < deviceVerifyTick_) return;
    deviceVerifyLogged_ = true;
    deviceVerifyTick_ = 0;
    if (outputDeviceId_.empty()) return;
    const std::string actual = appVolume_.findSessionDeviceId();
    if (actual.empty()) {
        AppLog::info("VlcEngine", "输出设备核对：还没有音频会话（未出声），跳过");
        return;
    }
    if (actual == outputDeviceId_) {
        confirmedDeviceId_ = outputDeviceId_;
        AppLog::info("VlcEngine", std::string("输出设备核对：请求=") + outputDeviceId_ + "  会话实际=" +
                                   actual + "  ✅ 已生效");
        return;
    }
    // 首次起播时 aout 还没建好，开播前设的设备会被 VLC 丢掉。
    // 这里在「已有 aout」的状态下再设一次（最多重试 2 次），再回读确认。
    if (deviceVerifyAttempts_ < 2) {
        ++deviceVerifyAttempts_;
        AppLog::info("VlcEngine", "输出设备未生效，第 " + std::to_string(deviceVerifyAttempts_) +
                                      " 次在已起播的 player 上重设设备");
        if (api_->libvlc_audio_output_set != nullptr) {
            api_->libvlc_audio_output_set(player_, "mmdevice");
        }
        api_->libvlc_audio_output_device_set(player_, "mmdevice", outputDeviceId_.c_str());
        deviceVerifyLogged_ = false;
        deviceVerifyTick_ = GetTickCount64() + 800;
        return;
    }
    AppLog::warn("VlcEngine", std::string("输出设备核对：请求=") + outputDeviceId_ + "  会话实际=" +
                                 actual + "  ❌ 未生效（VLC 仍输出到旧设备）");
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

bool VlcEngine::ended() const {
    if (!api_ || !player_ || api_->libvlc_media_player_get_state == nullptr) return false;
    // libvlc_state_t::libvlc_Ended == 6
    return api_->libvlc_media_player_get_state(player_) == 6;
}

int64_t VlcEngine::positionMs() const {
    if (!api_ || !player_) return 0;
    return api_->libvlc_media_player_get_time(player_);
}

int64_t VlcEngine::lengthMs() const {
    if (!api_ || !player_) return 0;
    return api_->libvlc_media_player_get_length(player_);
}

int VlcEngine::seekMs(int64_t ms) {
    if (!api_ || !player_) return -1;
    return api_->libvlc_media_player_set_time(player_, ms);
}

void VlcEngine::setVolume(int value) {
    volume_ = value < 0 ? 0 : (value > 100 ? 100 : value);
    volumeDirty_ = true;
    pumpVolume();
}

bool VlcEngine::setVolumeDevice(const std::wstring& deviceId) {
    volumeApplied_ = false;
    volumeDirty_ = true;
    const bool ok = appVolume_.setDevice(deviceId);
    appVolume_.resetRetryThrottle();
    pumpVolume();
    return ok;
}

void VlcEngine::pumpVolume() {
    verifyOutputDevice();
    if (!api_ || !player_) return;

    if (sessionVolumeEnabled_) {
        // 只用 WASAPI 应用级音量；会话正在重建（换曲/恢复）时**不要**回退去写 VLC 音量，
        // 否则会在 100%（WASAPI）和 cbrt(80)≈43（VLC）之间来回跳，听感就是「一炸一炸」。
        if (!appVolume_.refresh() || !appVolume_.available()) return;
        // 每帧都读回真实会话音量，只有不一致时才写（写动作本身才会产生可闻跳变）。
        // 旧实现每 250ms 才检查一次：换曲后系统把会话音量重置回 100% 时，
        // 最长会有 250ms 的「突然变响」——用户听到的换曲炸音就是它。
        const int before = appVolume_.volume();
        const bool forced = !volumeApplied_ || volumeDirty_;
        if (!appVolume_.setVolume(volume_, forced)) return;
        if (forced) {
            if (!volumeApplied_) {
                AppLog::info("AppVolume", "换曲后重新写入应用级音量 -> " +
                                              std::to_string(volume_) + "%");
            }
            if (lastVlcVolume_ != 100) {
                api_->libvlc_audio_set_volume(player_, 100);
                lastVlcVolume_ = 100;
            }
            volumeApplied_ = true;
            volumeDirty_ = false;
        } else if (before >= 0 && (before - volume_ > 1 || volume_ - before > 1)) {
            AppLog::info("AppVolume", "会话音量被重置为 " + std::to_string(before) + "%（应为 " +
                                          std::to_string(volume_) + "%），已纠正");
        }
        return;
    }

    // 回退到 VLC 时用和 Java 版一致的立方根曲线：
    //   VLC 的音量是立方响应，直接给百分比听感不对，反推 vlcVolume = 100 * cbrt(volume/100)
    int vlcVolume = (int)std::lround(100.0 * std::cbrt((double)volume_ / 100.0));
    vlcVolume = vlcVolume < 0 ? 0 : (vlcVolume > 100 ? 100 : vlcVolume);
    const int actual = (api_->libvlc_audio_get_volume != nullptr)
                           ? api_->libvlc_audio_get_volume(player_)
                           : -1;
    if (!volumeApplied_ || volumeDirty_ || lastVlcVolume_ != vlcVolume ||
        (actual >= 0 && actual != vlcVolume)) {
        api_->libvlc_audio_set_volume(player_, vlcVolume);
        lastVlcVolume_ = vlcVolume;
        volumeApplied_ = true;
        volumeDirty_ = false;
    }
}
