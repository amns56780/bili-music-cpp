#pragma once

#include <array>

// 真实音频频谱（WASAPI loopback → FFT），对应 Java 版 AudioSpectrum.kt。
// 采集默认输出设备的混音（别人听什么就抓什么），1024 点 FFT 压成 32 个对数频段。
// 采集失败时安静降级：所有频段保持 0，不影响播放。
namespace media {

class AudioSpectrum {
public:
    static constexpr int kBands = 32;

    static void start();
    static void stop();

    // 32 个频段能量（0..1，从低频到高频）的快照
    static std::array<float, kBands> bands();
};

}  // namespace media
