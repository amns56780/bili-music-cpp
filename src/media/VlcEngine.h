#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "media/AppVolume.h"

// libVLC 播放内核封装。
//
// 设计取舍：不链接 vlc 的 .lib，而是运行时 LoadLibrary + GetProcAddress。
//   1) 打包时用户机器上有没有 VLC 的 SDK 都无所谓，只要有 libvlc.dll；
//   2) 可以按「程序目录/vlc → 系统安装的 VLC」顺序找内核，和 Java 版的行为一致。
// 等 UI 稳定后再换成官方头文件（include/vlc/*.h）以拿到完整结构体/事件。
class VlcEngine {
public:
    VlcEngine();
    ~VlcEngine();

    VlcEngine(const VlcEngine&) = delete;
    VlcEngine& operator=(const VlcEngine&) = delete;

    // 加载 libvlc 并创建实例；失败时 error 里是排查信息（尝试过哪些路径）
    bool init(std::string* error);
    void shutdown();

    bool ready() const { return instance_ != nullptr; }
    std::string version() const;            // 例如 "3.0.21 Vetinari"
    std::wstring libraryDir() const { return libraryDir_; }

    bool playPath(const std::wstring& path, std::string* error);                     // 本地文件
    bool playUrl(const std::string& url, const std::string& referrer,
                 const std::string& userAgent, std::string* error);                  // 网络流

    // 网络缓存范围（毫秒）：越大越抗卡顿，但拖动进度条后重新出声越慢
    void setNetworkCacheMs(int ms) { networkCacheMs_ = ms < 1000 ? 1000 : ms; }
    int networkCacheMs() const { return networkCacheMs_; }

    // 音频输出设备（WASAPI/mmdevice）：列设备、切设备（换设备在下一首/恢复时生效）
    struct AudioDevice {
        std::string id;
        std::string name;
    };
    std::vector<AudioDevice> audioOutputDevices() const;
    void setAudioOutputDevice(const std::string& id) { outputDeviceId_ = id; }
    std::string audioOutputDevice() const { return outputDeviceId_; }
    void applyOutputDevice();  // 对当前播放器应用已选设备

    void togglePause();
    void resumeByPlay();  // 用 libvlc_media_player_play 恢复（自检对比用）
    void pause();
    void resume();
    void stop();

    bool isPlaying() const;
    int64_t positionMs() const;
    int64_t lengthMs() const;
    void seekMs(int64_t ms);

    // 音量：优先走 WASAPI 应用级音量（绕开 VLC，立即生效）；不可用时回退 libvlc。
    // App 每帧调用 pumpVolume()，保证 VLC 自己重置会话音量时也能被纠正回来。
    int volume() const { return volume_; }
    void setVolume(int value);
    void pumpVolume();

private:
    void releasePlayer();

    void* library_ = nullptr;                 // HMODULE
    void* instance_ = nullptr;                // libvlc_instance_t*
    void* media_ = nullptr;                   // libvlc_media_t*
    void* player_ = nullptr;                  // libvlc_media_player_t*
    std::wstring libraryDir_;

    struct Api;
    Api* api_ = nullptr;

    // 暂停/恢复：暂停久了再直接 set_pause(0) 容易在恢复瞬间断断续续，
    // 恢复前先 seek 到当前位置强制刷新 demux/音频输出。
    bool pausedRequested_ = false;
    std::chrono::steady_clock::time_point pausedAt_{};
    std::string outputDeviceId_;
    int networkCacheMs_ = 5000;

    media::AppVolume appVolume_;
    bool sessionVolumeEnabled_ = false;
    int volume_ = 80;

    // 音量写出节流：每帧写 WASAPI / VLC 音量会听到「一炸一炸」的杂音，
    // 只在音量真的变化 / 刚起播 / 会话重建时写，其余时间 250ms 检查一次会话状态。
    bool volumeDirty_ = true;
    bool volumeApplied_ = false;
    uint64_t volumeCheckTick_ = 0;
    int lastVlcVolume_ = -1;
};
