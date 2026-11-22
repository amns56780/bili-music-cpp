#include "media/AppVolume.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "util/AppLog.h"

// mingw-w64 的 endpointvolume.h 只前向声明了 IAudioMeterInformation，没有接口定义，
// 这里按 COM ABI 自己补一个（只用 GetPeakValue，vtable 顺序与官方一致）。
struct IAudioMeterInformation : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* pfPeak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* pnChannelCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 u32ChannelCount,
                                                           float* afPeakValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* pdwHardwareSupportMask) = 0;
};

namespace media {

namespace {

// 自己声明 GUID：MinGW/GCC 没有 MSVC 的 __uuidof，直接用坐标常量最稳
const GUID kClsidMmDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidMmDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidAudioSessionManager2 = {
    0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
const GUID kIidAudioSessionControl2 = {
    0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};
const GUID kIidSimpleAudioVolume = {
    0x87CE5498, 0x68D6, 0x44E5, {0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8}};
const GUID kIidAudioMeterInformation = {
    0xC02216F6, 0x8C67, 0x4B5B, {0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64}};

std::string wideToUtf8Local(const wchar_t* text) {
    if (text == nullptr) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out((size_t)(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size - 1, nullptr, nullptr);
    return out;
}

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return text;
}

bool sameDeviceId(const std::string& a, const std::string& b) {
    return !a.empty() && !b.empty() && lowerAscii(a) == lowerAscii(b);
}

template <typename T>
void safeRelease(T*& pointer) {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

}  // namespace

bool AppVolume::init() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comInitialized_ = SUCCEEDED(comResult);

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(kClsidMmDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  kIidMmDeviceEnumerator, (void**)&enumerator);
    if (FAILED(hr) || enumerator == nullptr) {
        AppLog::info("AppVolume", "WASAPI 设备枚举器创建失败");
        return false;
    }
    enumerator_ = enumerator;

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device_);
    if (FAILED(hr) || device_ == nullptr) {
        AppLog::info("AppVolume", "拿不到默认输出设备，音量回退到 VLC");
        return false;
    }
    hr = device_->Activate(kIidAudioSessionManager2, CLSCTX_ALL, nullptr,
                           (void**)&sessionManager_);
    if (FAILED(hr) || sessionManager_ == nullptr) {
        AppLog::info("AppVolume", "AudioSessionManager 激活失败，音量回退到 VLC");
        return false;
    }
    return true;
}

void AppVolume::releaseSessions() {
    for (BoundSession& bound : sessions_) {
        safeRelease(bound.volume);
        safeRelease(bound.control);
        safeRelease(bound.meter);
    }
    sessions_.clear();
}

void AppVolume::shutdown() {
    releaseSessions();
    safeRelease(sessionManager_);
    safeRelease(device_);
    safeRelease(enumerator_);
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

bool AppVolume::refresh() {
    if (sessions_.empty()) return ensureSession();
    // 目标设备变了（App 热切换输出设备）：限频重扫
    if (!targetDeviceId_.empty() && !sameDeviceId(targetDeviceId_, boundDeviceId_)) {
        const uint64_t now = GetTickCount64();
        if (now - lastActiveCheckTick_ < 500) return true;
        lastActiveCheckTick_ = now;
        releaseSessions();
        lastRetryTick_ = 0;
        return ensureSession();
    }
    for (const BoundSession& bound : sessions_) {
        AudioSessionState state = AudioSessionStateExpired;
        const bool stateOk =
            bound.control != nullptr && SUCCEEDED(bound.control->GetState(&state));
        if (!stateOk || state == AudioSessionStateExpired) {
            // Inactive 在「暂停」时是正常的，不重绑；Expired 才需要重新枚举。
            releaseSessions();
            lastRetryTick_ = 0;
            AppLog::info("AppVolume", "音频会话已失效，重新绑定");
            return ensureSession();
        }
    }
    return true;
}

std::string AppVolume::findSessionDeviceId() {
    if (enumerator_ == nullptr) return {};
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) ||
        collection == nullptr) {
        return {};
    }
    UINT deviceCount = 0;
    collection->GetCount(&deviceCount);
    const DWORD currentPid = GetCurrentProcessId();
    std::string foundActive;
    std::string foundInactive;
    for (UINT d = 0; d < deviceCount && foundActive.empty(); ++d) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(d, &device)) || device == nullptr) continue;
        IAudioSessionManager2* manager = nullptr;
        if (SUCCEEDED(device->Activate(kIidAudioSessionManager2, CLSCTX_ALL, nullptr,
                                       (void**)&manager)) &&
            manager != nullptr) {
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && sessions != nullptr) {
                int sessionCount = 0;
                sessions->GetCount(&sessionCount);
                for (int s = 0; s < sessionCount; ++s) {
                    IAudioSessionControl* control = nullptr;
                    if (FAILED(sessions->GetSession(s, &control)) || control == nullptr) continue;
                    IAudioSessionControl2* control2 = nullptr;
                    if (SUCCEEDED(control->QueryInterface(kIidAudioSessionControl2,
                                                          (void**)&control2)) &&
                        control2 != nullptr) {
                        DWORD pid = 0;
                        control2->GetProcessId(&pid);
                        AudioSessionState state = AudioSessionStateExpired;
                        control2->GetState(&state);
                        // 过期会话会残留在旧设备上 —— 必须排除，否则会把"旧设备"误判成当前设备。
                        // 同时优先认 Active（真正在出声）的会话，Inactive 只做兜底。
                        if (pid == currentPid && state != AudioSessionStateExpired) {
                            LPWSTR id = nullptr;
                            if (SUCCEEDED(device->GetId(&id)) && id != nullptr) {
                                const std::string deviceId = wideToUtf8Local(id);
                                CoTaskMemFree(id);
                                if (state == AudioSessionStateActive) {
                                    if (foundActive.empty()) foundActive = deviceId;
                                } else if (foundInactive.empty()) {
                                    foundInactive = deviceId;
                                }
                            }
                        }
                        control2->Release();
                    }
                    control->Release();
                }
                sessions->Release();
            }
            manager->Release();
        }
        device->Release();
    }
    collection->Release();
    return foundActive.empty() ? foundInactive : foundActive;
}

bool AppVolume::ensureSession() {
    if (!sessions_.empty()) return true;
    if (enumerator_ == nullptr) return false;

    // 新音频会话（换曲/换输出设备）会话音量由系统重置为 100%，
    // 这里清掉旧的缓存值，保证下面 setVolume 一定会把用户音量重新写进去。
    lastVolume_ = -1;

    // 会话在第一次出声之后才创建：限频重试，别每一帧都枚举一遍。
    // 100ms 足够让换曲后新会话在 1~2 帧内被接管（旧值 500ms 会让换曲炸音持续半秒）。
    const uint64_t now = GetTickCount64();
    if (lastRetryTick_ != 0 && now - lastRetryTick_ < 100) return false;
    lastRetryTick_ = now;

    // 目标设备：App 指定了就用它，否则用系统默认设备。
    std::string target = targetDeviceId_;
    if (target.empty()) {
        IMMDevice* defaultDevice = nullptr;
        if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice)) &&
            defaultDevice != nullptr) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(defaultDevice->GetId(&id)) && id != nullptr) {
                target = wideToUtf8Local(id);
                CoTaskMemFree(id);
            }
            defaultDevice->Release();
        }
    }

    // 扫描本进程在所有活动渲染端点上的会话，按「目标设备精确匹配」优先。
    // 注意：本机实测 VLC 的会话状态不可靠（播放中也可能报 Inactive，旧设备的会话还可能残留
    // 成 Active），所以不能只按状态挑 —— 那会把音量写回旧设备，新设备音量调不动。
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) ||
        collection == nullptr) {
        return false;
    }
    struct Candidate {
        ISimpleAudioVolume* volume = nullptr;
        IAudioSessionControl2* control = nullptr;
        IAudioMeterInformation* meter = nullptr;
        std::string deviceId;
        int state = 1;
    };
    std::vector<Candidate> candidates;
    UINT deviceCount = 0;
    collection->GetCount(&deviceCount);
    const DWORD currentPid = GetCurrentProcessId();
    for (UINT d = 0; d < deviceCount; ++d) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(d, &device)) || device == nullptr) continue;
        std::string deviceId;
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)) && id != nullptr) {
            deviceId = wideToUtf8Local(id);
            CoTaskMemFree(id);
        }
        IAudioSessionManager2* manager = nullptr;
        if (SUCCEEDED(device->Activate(kIidAudioSessionManager2, CLSCTX_ALL, nullptr,
                                       (void**)&manager)) &&
            manager != nullptr) {
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && sessions != nullptr) {
                int count = 0;
                sessions->GetCount(&count);
                for (int i = 0; i < count; ++i) {
                    IAudioSessionControl* control = nullptr;
                    if (FAILED(sessions->GetSession(i, &control)) || control == nullptr) continue;
                    IAudioSessionControl2* control2 = nullptr;
                    if (SUCCEEDED(control->QueryInterface(kIidAudioSessionControl2,
                                                          (void**)&control2)) &&
                        control2 != nullptr) {
                        DWORD pid = 0;
                        control2->GetProcessId(&pid);
                        AudioSessionState state = AudioSessionStateExpired;
                        control2->GetState(&state);
                        if (pid == currentPid && state != AudioSessionStateExpired) {
                            ISimpleAudioVolume* simple = nullptr;
                            if (SUCCEEDED(control2->QueryInterface(kIidSimpleAudioVolume,
                                                                   (void**)&simple)) &&
                                simple != nullptr) {
                                Candidate candidate;
                                candidate.volume = simple;
                                candidate.control = control2;
                                candidate.deviceId = deviceId;
                                candidate.state = (int)state;
                                control2 = nullptr;  // 所有权转移
                                IAudioMeterInformation* meter = nullptr;
                                if (SUCCEEDED(candidate.control->QueryInterface(
                                        kIidAudioMeterInformation, (void**)&meter)) &&
                                    meter != nullptr) {
                                    candidate.meter = meter;
                                }
                                candidates.push_back(candidate);
                            }
                        }
                        if (control2 != nullptr) control2->Release();
                    }
                    control->Release();
                }
                sessions->Release();
            }
            manager->Release();
        }
        device->Release();
    }
    collection->Release();

    // 注意：同一条设备上可能同时存在本进程的两个会话（VLC 重建音频输出后旧会话可能残留），
    // 只挑一个会挑到“不出声”的那个：音量写进去没效果。这里把该设备上的会话全部绑定。
    const bool explicitTarget = !targetDeviceId_.empty();
    std::vector<int> picked;
    if (!target.empty()) {
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (sameDeviceId(candidates[i].deviceId, target)) picked.push_back((int)i);
        }
    }
    if (picked.empty() && !explicitTarget) {
        // 跟随默认设备：默认设备没有会话时兜底（优先 Active，其次任意）
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].state == (int)AudioSessionStateActive) picked.push_back((int)i);
        }
        if (picked.empty()) {
            for (size_t i = 0; i < candidates.size(); ++i) picked.push_back((int)i);
        }
    }
    if (picked.empty()) {
        for (Candidate& candidate : candidates) {
            if (candidate.volume != nullptr) candidate.volume->Release();
            if (candidate.control != nullptr) candidate.control->Release();
            if (candidate.meter != nullptr) candidate.meter->Release();
        }
        return false;
    }

    std::vector<bool> keep(candidates.size(), false);
    for (int index : picked) keep[(size_t)index] = true;
    releaseSessions();
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!keep[i]) {
            if (candidates[i].volume != nullptr) candidates[i].volume->Release();
            if (candidates[i].control != nullptr) candidates[i].control->Release();
            if (candidates[i].meter != nullptr) candidates[i].meter->Release();
            continue;
        }
        sessions_.push_back(BoundSession{candidates[i].volume, candidates[i].control,
                                         candidates[i].meter});
    }
    boundDeviceId_ = candidates[(size_t)picked.front()].deviceId;
    AppLog::info("AppVolume", "已接入 WASAPI 应用级音量（设备 " + boundDeviceId_ + "，" +
                                  std::to_string(sessions_.size()) + " 个会话）");
    return true;
}

float AppVolume::uiToLevel(int percent) {
    const int ui = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    if (ui <= 0) return 0.0f;
    if (ui >= 100) return 1.0f;
    // 低音量段（1..10）压到 0.1%..1%，避免夜深时最小音量还是太响；
    // 11..99 走 1.1 次幂，比线性略陡，和 Java 版听感一致。
    if (ui <= 10) return 0.001f * (float)ui;
    const float t = (float)(ui - 10) / 90.0f;
    return 0.01f + 0.99f * std::pow(t, 1.1f);
}

int AppVolume::levelToUi(float level) {
    if (level <= 0.0f) return 0;
    if (level >= 1.0f) return 100;
    if (level <= 0.01f) {
        const int ui = (int)std::lround(level / 0.001f);
        return ui < 1 ? 1 : (ui > 10 ? 10 : ui);
    }
    const float t = (level - 0.01f) / 0.99f;
    const int ui = (int)std::lround(10.0f + 90.0f * std::pow(t, 1.0f / 1.1f));
    return ui < 11 ? 11 : (ui > 99 ? 99 : ui);
}

bool AppVolume::setDevice(const std::wstring& endpointId) {
    // 记下目标设备：热切换后要严格绑到这台设备上的会话。
    // （旧设备的会话可能还残留着 Active，只按状态挑会写回旧设备，新设备音量就调不动。）
    targetDeviceId_ = wideToUtf8Local(endpointId.c_str());
    releaseSessions();
    lastRetryTick_ = 0;
    lastActiveCheckTick_ = 0;
    return ensureSession();
}

bool AppVolume::setVolume(int percent, bool force) {
    const int clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    if (!ensureSession()) return false;
    const float target = uiToLevel(clamped);
    int before = -1;
    for (BoundSession& bound : sessions_) {
        if (bound.volume == nullptr) continue;
        float level = 0.0f;
        const bool haveLevel = SUCCEEDED(bound.volume->GetMasterVolume(&level));
        // 读回真实电平而不是只比缓存：系统在换曲 / 会话重建时可能把音量改回 100%，
        // 必须按真实值判断，否则会漏掉这种“被外部改掉”的情况。
        if (!force && haveLevel && std::fabs(level - target) < 0.0002f) continue;
        if (!force && before < 0 && haveLevel) before = levelToUi(level);
        bound.volume->SetMasterVolume(target, nullptr);
    }
    if (!force && before >= 0 && (before - clamped > 1 || clamped - before > 1)) {
        AppLog::info("AppVolume", "会话音量被重置为 " + std::to_string(before) + "%（应为 " +
                                      std::to_string(clamped) + "%），已纠正");
    }
    if (lastVolume_ != clamped) {
        lastVolume_ = clamped;
        if (force) AppLog::info("AppVolume", "应用级音量 -> " + std::to_string(clamped) + "%");
    }
    return true;
}

int AppVolume::volume() const {
    if (sessions_.empty() || sessions_.front().volume == nullptr) return -1;
    float level = 0.0f;
    if (FAILED(sessions_.front().volume->GetMasterVolume(&level))) return -1;
    return levelToUi(level);
}

float AppVolume::peak() const {
    float best = -1.0f;
    for (const BoundSession& bound : sessions_) {
        if (bound.meter == nullptr) continue;
        float value = 0.0f;
        if (SUCCEEDED(bound.meter->GetPeakValue(&value)) && value > best) best = value;
    }
    return best;
}

}  // namespace media
