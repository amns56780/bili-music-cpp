#pragma once

#include <cstdint>
#include <string>

struct IMMDevice;
struct IMMDeviceEnumerator;
struct IAudioSessionManager2;
struct ISimpleAudioVolume;
struct IAudioMeterInformation;
struct IAudioSessionControl2;

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

    // 音量曲线（与 Java 版 PlaybackController.uiToSessionVolume 完全一致）：
    //   0 -> 0；1..10 -> 0.001*u；11..99 -> 0.01 + 0.99*((u-10)/90)^1.1；100 -> 1
    // 直接用 u/100 会让低音量段比 Java 版响 10 倍（1% 是 0.01 而不是 0.001）。
    static float uiToLevel(int percent);
    static int levelToUi(float level);
    int volume() const;  // 读回当前会话音量（0..100）；不可用时返回 -1
    float peak() const;  // 当前音频会话峰值（0..1），用于自检「断断续续」

    // 会话失效（VLC 重建音频输出）时重新绑定；返回当前是否可用
    bool refresh();

    // 改绑到指定输出设备（空 = 系统默认）。App 热切换输出设备后必须调用：
    // 原来的会话管理器属于旧设备，继续写只会改到旧设备上的会话，新设备音量调不动。
    bool setDevice(const std::wstring& endpointId);

    // 换曲 / 起播时清掉重试节流，让首次绑定不必等 100ms
    void resetRetryThrottle() { lastRetryTick_ = 0; }

    // 扫所有活动渲染端点，找出本进程音频会话实际挂在哪个设备上（返回 endpoint id，找不到返回空串）
    // 用途：切换输出设备后验证「真的换了路由」，而不只是界面上显示换了
    std::string findSessionDeviceId();

private:
    bool ensureSession();

    bool comInitialized_ = false;
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioSessionManager2* sessionManager_ = nullptr;
    ISimpleAudioVolume* sessionVolume_ = nullptr;
    IAudioMeterInformation* meter_ = nullptr;
    IAudioSessionControl2* sessionControl_ = nullptr;
    int lastVolume_ = -1;
    uint64_t lastRetryTick_ = 0;
    uint64_t lastActiveCheckTick_ = 0;
    std::string targetDeviceId_;  // App 指定的输出设备（空 = 系统默认）
    std::string boundDeviceId_;   // 当前绑定的会话所在设备
};

}  // namespace media
