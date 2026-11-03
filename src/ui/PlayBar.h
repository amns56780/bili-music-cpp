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
    bool connectDevice = false;
    bool download = false;
};

// 底部播放条（76px）：当前曲目 / 控制 + 进度 / 音量 + 输出设备 / 定时 / 队列。
// 对应 Java 版 PlayerBar.kt。
void DrawPlayBar(const PlayBarData& data, PlayBarActions* actions, float width, float height);

}  // namespace Ui
