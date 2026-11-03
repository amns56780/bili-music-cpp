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

#include <cmath>

#include "util/AppLog.h"

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

void AppVolume::shutdown() {
    safeRelease(sessionVolume_);
    safeRelease(sessionManager_);
    safeRelease(device_);
    safeRelease(enumerator_);
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

bool AppVolume::ensureSession() {
    if (sessionVolume_ != nullptr) return true;
    if (sessionManager_ == nullptr) return false;

    // 会话在第一次出声之后才创建：限频重试，别每一帧都枚举一遍
    const uint64_t now = GetTickCount64();
    if (lastRetryTick_ != 0 && now - lastRetryTick_ < 500) return false;
    lastRetryTick_ = now;

    IAudioSessionEnumerator* sessions = nullptr;
    if (FAILED(sessionManager_->GetSessionEnumerator(&sessions)) || sessions == nullptr) return false;

    const DWORD currentPid = GetCurrentProcessId();
    int count = 0;
    sessions->GetCount(&count);
    for (int i = 0; i < count && sessionVolume_ == nullptr; ++i) {
        IAudioSessionControl* control = nullptr;
        if (FAILED(sessions->GetSession(i, &control)) || control == nullptr) continue;
        IAudioSessionControl2* control2 = nullptr;
        if (SUCCEEDED(control->QueryInterface(kIidAudioSessionControl2, (void**)&control2)) &&
            control2 != nullptr) {
            DWORD pid = 0;
            control2->GetProcessId(&pid);
            if (pid == currentPid) {
                ISimpleAudioVolume* simple = nullptr;
                if (SUCCEEDED(control2->QueryInterface(kIidSimpleAudioVolume, (void**)&simple)) &&
                    simple != nullptr) {
                    sessionVolume_ = simple;
                }
            }
            control2->Release();
        }
        control->Release();
    }
    sessions->Release();
    if (sessionVolume_ != nullptr) {
        AppLog::info("AppVolume", "已接入 WASAPI 应用级音量");
        return true;
    }
    return false;
}

bool AppVolume::setVolume(int percent, bool force) {
    const int clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    if (!ensureSession()) return false;
    if (!force && lastVolume_ == clamped) return true;
    const float level = (float)clamped / 100.0f;
    if (FAILED(sessionVolume_->SetMasterVolume(level, nullptr))) return false;
    if (lastVolume_ != clamped) {
        AppLog::info("AppVolume", "应用级音量 -> " + std::to_string(clamped) + "%");
    }
    lastVolume_ = clamped;
    return true;
}

int AppVolume::volume() const {
    if (sessionVolume_ == nullptr) return -1;
    float level = 0.0f;
    if (FAILED(sessionVolume_->GetMasterVolume(&level))) return -1;
    return (int)std::lround(level * 100.0f);
}

}  // namespace media
