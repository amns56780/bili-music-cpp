#pragma once

#include <cstdint>
#include <string>

namespace Ui {

struct PlayBarData {
    bool playing = false;
    bool hasMedia = false;
    double positionMs = 0.0;
    double durationMs = 0.0;
    int volume = 80;
    int mode = 1;  // 0=SEQUENTIAL 1=REPEAT_ALL 2=REPEAT_ONE 3=SHUFFLE
    std::string title = "还没有在播放";
    std::string artist = "选一个歌单，或粘贴 BV 号";
    // 当前曲目封面（优先视频封面，没有就退化成歌单封面）
    std::string coverUrl;
    std::string coverPath;
    bool immersive = false;  // 沉浸模式：全屏按钮显示「退出全屏」图标
    // 在线曲目才有音质档位
    bool showQuality = false;
    int qualityId = 0;  // 0=自动，其它为 B 站音质 id（30280/30232/30216/30251/30250）
    std::string qualityLabel = "自动";
};

struct PlayBarActions {
    bool togglePlay = false;
    bool next = false;
    bool previous = false;
    bool cycleMode = false;
    bool seekFinished = false;
    double seekMs = 0.0;
    bool volumeChanged = false;
    bool volumeFinished = false;
    int volume = 80;
    bool openQueue = false;
    bool openOutputDevice = false;
    bool openSleepTimer = false;
    bool toggleImmersive = false;
    bool connectDevice = false;
    bool download = false;
    bool qualitySelected = false;
    int qualityId = 0;
    float volumeWheelDelta = 0.0f;  // 鼠标滚轮微调音量（正值增大）
};

// 底部播放条（76px）：当前曲目 / 控制 + 进度 / 音量 + 输出设备 / 定时 / 队列。
// 对应 Java 版 PlayerBar.kt。
void DrawPlayBar(const PlayBarData& data, PlayBarActions* actions, float width, float height);

}  // namespace Ui
