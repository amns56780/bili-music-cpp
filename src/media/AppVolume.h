#pragma once

#include <cstdint>

struct IMMDevice;
struct IMMDeviceEnumerator;
struct IAudioSessionManager2;
struct ISimpleAudioVolume;

namespace media {

// 应用级音量：Windows WASAPI 的「每应用音量」（ISimpleAudioVolume）。
//
// 为什么不走 VLC：Java 版把音量交给 VLC -> libvlc_audio_set_volume，要经过 JNA、
// VLC 内部音量平滑、音频缓冲，手感明显滞后。这里直接改本进程音频会话的音量，
// 是系统混音器那一层，立即生效，和网易云等原生播放器同一路由。
//
// 约束：会话要等 libVLC 真正开始输出音频后才会出现，所以这里是懒获取 + 限频重试。
class AppVolume {
public:
    bool init();
    void shutdown();

    bool available() const { return sessionVolume_ != nullptr; }

    // percent: 0..100；force=true 时即使数值没变也强制下发（防止 VLC 自己把会话音量改回去）
    bool setVolume(int percent, bool force = false);
    int volume() const;  // 读回当前会话音量（0..100）；不可用时返回 -1

private:
    bool ensureSession();

    bool comInitialized_ = false;
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioSessionManager2* sessionManager_ = nullptr;
    ISimpleAudioVolume* sessionVolume_ = nullptr;
    int lastVolume_ = -1;
    uint64_t lastRetryTick_ = 0;
};

}  // namespace media
