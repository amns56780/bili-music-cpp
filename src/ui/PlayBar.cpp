#include "ui/PlayBar.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace Ui {

namespace {

std::string formatTime(double ms) {
    if (ms <= 0.0) return "0:00";
    const long long totalSeconds = (long long)(ms / 1000.0);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", totalSeconds / 60, totalSeconds % 60);
    return buffer;
}

uint32_t modeIcon(int mode) {
    switch (mode) {
        case 0: return Icons::PlaylistPlay;
        case 2: return Icons::RepeatOne;
        case 3: return Icons::Shuffle;
        default: return Icons::Repeat;
    }
}

const char* modeName(int mode) {
    switch (mode) {
        case 0: return "顺序播放";
        case 2: return "单曲循环";
        case 3: return "随机播放";
        default: return "列表循环";
    }
}

}  // namespace

void DrawPlayBar(const PlayBarData& data, PlayBarActions* actions, float width, float height) {
    const ImVec2 windowPos = ImGui::GetWindowPos();
    auto abs = [&](float x, float y) { return ImVec2(windowPos.x + x, windowPos.y + y); };
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // ---------------- 左：当前曲目 ----------------
    CoverPlaceholder(data.title, abs(18.0f, 13.0f), Dimens::CoverPlayer, 9.0f);
    TextEllipsis(data.title, 236.0f, GetFonts().body, Colors::Text, abs(80.0f, 22.0f));
    TextEllipsis(data.artist, 236.0f, GetFonts().small, Colors::Text3, abs(80.0f, 42.0f));
    ImGui::SetCursorPos(ImVec2(18.0f + 330.0f - 32.0f, 22.0f));
    if (IconButton("download_current", Icons::Download, 32.0f, 17.0f, Colors::Text2, false,
                   data.hasMedia, "下载当前曲目（M3）")) {
        actions->download = true;
    }

    // ---------------- 中：控制 + 进度 ----------------
    const float centerX = width / 2.0f;
    const float controlsWidth = 238.0f;
    const float controlsLeft = centerX - controlsWidth / 2.0f;
    ImGui::SetCursorPos(ImVec2(controlsLeft, 12.0f));
    if (IconButton("mode", modeIcon(data.mode), 32.0f, 17.0f, Colors::Text2, false, true, modeName(data.mode))) {
        actions->cycleMode = true;
    }
    ImGui::SetCursorPos(ImVec2(controlsLeft + 50.0f, 12.0f));
    if (IconButton("prev", Icons::SkipPrevious, 32.0f, 20.0f, Colors::Text, false, data.hasMedia, "上一首")) {
        actions->previous = true;
    }
    ImGui::SetCursorPos(ImVec2(controlsLeft + 100.0f, 12.0f));
    if (PlayCircleButton("playpause", data.playing, 38.0f)) actions->togglePlay = true;
    ImGui::SetCursorPos(ImVec2(controlsLeft + 156.0f, 12.0f));
    if (IconButton("next", Icons::SkipNext, 32.0f, 20.0f, Colors::Text, false, data.hasMedia, "下一首")) {
        actions->next = true;
    }

    // 进度条（拖动中只改预览，松手才 seek；和 Java 版 BmSlider 的行为一致）
    static bool seeking = false;
    static float seekDraft = 0.0f;
    const float progressLeft = 18.0f + 330.0f + 18.0f;
    const float progressRight = width - 18.0f - 350.0f - 18.0f;
    const float progressWidth = progressRight - progressLeft;
    const float ratio = data.durationMs > 0.0 ? (float)(data.positionMs / data.durationMs) : 0.0f;
    const std::string posText = formatTime(seeking ? seekDraft * data.durationMs : data.positionMs);
    const std::string durText = data.durationMs > 0.0 ? formatTime(data.durationMs) : "--:--";
    draw->AddText(GetFonts().small, 12.0f, abs(progressLeft, 58.0f), Colors::Text3, posText.c_str());
    ImGui::SetCursorPos(ImVec2(progressLeft + 38.0f, 54.0f));
    float sliderValue = seeking ? seekDraft : (ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio));
    const SliderResult seekResult =
        ThinSlider("seek", &sliderValue, progressWidth - 76.0f, 14.0f, 4.0f, Colors::Brand);
    if (seekResult.changed) {
        seeking = true;
        seekDraft = sliderValue;
    }
    if (seekResult.finished) {
        seeking = false;
        actions->seekFinished = true;
        actions->seekMs = (double)seekDraft * data.durationMs;
    }
    const float durationWidth = MeasureText(GetFonts().small, 12.0f, durText);
    draw->AddText(GetFonts().small, 12.0f,
                  abs(progressRight - durationWidth, 58.0f), Colors::Text3, durText.c_str());

    // ---------------- 右：音量 / 输出设备 / 定时 / 队列 ----------------
    const float rightWidth = 290.0f;
    const float rightLeft = width - 18.0f - rightWidth;
    // 音量组
    const std::string volumeIcon =
        Icons::Utf8(data.volume <= 0 ? Icons::VolumeOff
                                     : (data.volume < 50 ? Icons::VolumeDown : Icons::VolumeUp));
    draw->AddText(GetFonts().body, 16.0f, abs(rightLeft, 30.0f), Colors::Text2, volumeIcon.c_str());
    static bool volumeDragging = false;
    static float volumeDraft = 0.8f;
    if (!volumeDragging) volumeDraft = data.volume / 100.0f;
    ImGui::SetCursorPos(ImVec2(rightLeft + 24.0f, 31.0f));
    float volumeValue = volumeDraft;
    const SliderResult volumeResult = ThinSlider("volume", &volumeValue, 110.0f, 14.0f, 4.0f, Colors::Text2);
    if (volumeResult.changed) {
        volumeDragging = true;
        volumeDraft = volumeValue;
        actions->volume = (int)std::lround(volumeValue * 100.0f);
        actions->volumeChanged = true;
    }
    if (volumeResult.finished) {
        volumeDragging = false;
        actions->volumeFinished = true;
        actions->volume = (int)std::lround(volumeDraft * 100.0f);
    }
    const std::string volumeText =
        (int)std::lround(volumeValue * 100.0f) == 0 ? "静音" :
        std::to_string((int)std::lround(volumeValue * 100.0f)) + "%";
    const float volumeTextWidth = MeasureText(GetFonts().small, 12.0f, volumeText);
    draw->AddText(GetFonts().small, 12.0f, abs(rightLeft + 24.0f + 110.0f + 6.0f + 34.0f - volumeTextWidth, 32.0f),
                  Colors::Text3, volumeText.c_str());
    ImGui::SetCursorPos(ImVec2(rightLeft + 182.0f, 22.0f));
    if (IconButton("output_device", Icons::Headset, 32.0f, 18.0f, Colors::Text2, false, true, "输出设备（M4）")) {
        actions->openOutputDevice = true;
    }
    ImGui::SetCursorPos(ImVec2(rightLeft + 220.0f, 22.0f));
    if (IconButton("sleep_timer", Icons::Bedtime, 32.0f, 18.0f, Colors::Text2, false, true, "定时关闭（M4）")) {
        actions->openSleepTimer = true;
    }
    ImGui::SetCursorPos(ImVec2(rightLeft + 258.0f, 22.0f));
    if (IconButton("queue", Icons::PlaylistPlay, 32.0f, 18.0f, Colors::Text2, false, true, "播放队列（M4）")) {
        actions->openQueue = true;
    }
}

}  // namespace Ui
