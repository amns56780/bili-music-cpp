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
    CoverPlaceholder(data.title, abs(18.0f, (height - Dimens::CoverPlayer) / 2.0f), Dimens::CoverPlayer, 9.0f);
    TextEllipsis(data.title, 236.0f, GetFonts().body, Colors::Text, abs(80.0f, 20.0f));
    TextEllipsis(data.artist, 236.0f, GetFonts().small, Colors::Text3, abs(80.0f, 39.0f));
    ImGui::SetCursorPos(ImVec2(18.0f + 330.0f - 32.0f, 11.0f));
    if (IconButton("download_current", Icons::Download, 32.0f, 17.0f, Colors::Text2, false,
                   data.hasMedia, "下载当前曲目（M3）")) {
        actions->download = true;
    }

    // ---------------- 中：控制 + 进度 ----------------
    const float centerX = width / 2.0f;
    const float controlsWidth = 238.0f;
    const float controlsLeft = centerX - controlsWidth / 2.0f;
    // 图标按钮的字形在行框里偏上（字体 ascent），所以命中框要比播放键的 y 再往下挪一点，
    // 视觉上才和其它元素同一条水平中心线（用户反馈的「除播放键外按钮偏上」）。
    ImGui::SetCursorPos(ImVec2(controlsLeft, 11.0f));
    if (IconButton("mode", modeIcon(data.mode), 32.0f, 17.0f, Colors::Text2, false, true, modeName(data.mode))) {
        actions->cycleMode = true;
    }
    ImGui::SetCursorPos(ImVec2(controlsLeft + 50.0f, 11.0f));
    if (IconButton("prev", Icons::SkipPrevious, 32.0f, 20.0f, Colors::Text, false, data.hasMedia, "上一首")) {
        actions->previous = true;
    }
    ImGui::SetCursorPos(ImVec2(controlsLeft + 100.0f, 8.0f));
    if (PlayCircleButton("playpause", data.playing, 38.0f)) actions->togglePlay = true;
    ImGui::SetCursorPos(ImVec2(controlsLeft + 156.0f, 11.0f));
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
    draw->AddText(GetFonts().small, 15.0f, abs(progressLeft, 49.0f), Colors::Text3, posText.c_str());
    ImGui::SetCursorPos(ImVec2(progressLeft + 38.0f, 50.0f));
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
    const float durationWidth = MeasureText(GetFonts().small, 15.0f, durText);
    draw->AddText(GetFonts().small, 15.0f,
                  abs(progressRight - durationWidth, 49.0f), Colors::Text3, durText.c_str());

    // ---------------- 右：音量 / 输出设备 / 定时 / 队列 ----------------
    const float rightWidth = 290.0f;
    const float rightLeft = width - 18.0f - rightWidth;
    // 音量组
    const std::string volumeIcon =
        Icons::Utf8(data.volume <= 0 ? Icons::VolumeOff
                                     : (data.volume < 50 ? Icons::VolumeDown : Icons::VolumeUp));
    // 右侧音量组和三个图标放在「播放键中心」与「进度条中心」之间（取中间线）：
    // 既不会像之前那样贴着播放键显得偏上，也不会沉到最底部和进度条挤在一起。
    const float controlsCenterY = 8.0f + 19.0f;       // 播放键 / 控制行中心（局部 y=27）
    const float progressCenterY = 50.0f + 7.0f;       // 进度条中心（局部 y=57）
    const float rightCenterY = (controlsCenterY + progressCenterY) * 0.5f - 4.0f;  // 38（用户要求再上移一点）

    // 音质档位（仅在线曲目）：进度条和音量组之间的空档里放一个小胶囊
    if (data.showQuality) {
        // 胶囊宽度按文字自适应（之前固定 66px，短档位会多出一截空白）
        const float labelWidth = MeasureText(GetFonts().small, FontSize::Small, data.qualityLabel);
        float chipWidth = 12.0f + labelWidth + 14.0f;
        const float maxChipWidth = rightLeft - (progressRight + 8.0f) - 6.0f;
        if (chipWidth > maxChipWidth) chipWidth = maxChipWidth;
        const float chipHeight = 24.0f;
        const float chipX = progressRight + 8.0f;
        const float chipY = rightCenterY - chipHeight / 2.0f;
        ImGui::PushID("quality_chip");
        ImGui::SetCursorPos(ImVec2(chipX, chipY));
        const bool qualityClicked = ImGui::InvisibleButton("##quality", ImVec2(chipWidth, chipHeight));
        const bool qualityHovered = ImGui::IsItemHovered();
        if (qualityHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddRectFilled(abs(chipX, chipY), abs(chipX + chipWidth, chipY + chipHeight),
                            qualityHovered ? Colors::Hover : Colors::TagBg, 11.0f);
        TextEllipsis(data.qualityLabel, chipWidth - 24.0f, GetFonts().small, Colors::Text2,
                     abs(chipX + 8.0f, chipY + 4.0f));
        // 下拉小三角用几何图形画（字体里没有 ▾ 字形）
        const ImVec2 arrow(chipX + chipWidth - 12.0f, chipY + chipHeight / 2.0f);
        draw->AddTriangleFilled(ImVec2(arrow.x - 4.0f, arrow.y - 2.0f),
                                ImVec2(arrow.x + 4.0f, arrow.y - 2.0f),
                                ImVec2(arrow.x, arrow.y + 3.0f), Colors::Text3);
        if (qualityClicked) ImGui::OpenPopup("quality_menu");
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Ui::Vec4(Ui::Colors::Dialog));
        ImGui::PushStyleColor(ImGuiCol_Text, Ui::Vec4(Ui::Colors::Text));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        if (ImGui::BeginPopup("quality_menu")) {
            struct QualityOption {
                int id;
                const char* label;
            };
            const QualityOption options[] = {
                {0, "自动（推荐）"}, {30280, "192K"},   {30232, "132K"},
                {30216, "64K"},      {30251, "Hi-Res 无损"}, {30250, "杜比全景声"},
            };
            for (const QualityOption& option : options) {
                const bool selected = option.id == data.qualityId;
                if (ImGui::Selectable(option.label, selected)) {
                    actions->qualitySelected = true;
                    actions->qualityId = option.id;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    draw->AddText(GetFonts().body, 18.0f, abs(rightLeft, rightCenterY - 7.0f), Colors::Text2,
                  volumeIcon.c_str());
    static bool volumeDragging = false;
    static float volumeDraft = 0.8f;
    if (!volumeDragging) volumeDraft = data.volume / 100.0f;
    ImGui::SetCursorPos(ImVec2(rightLeft + 24.0f, rightCenterY - 7.0f));
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
    const float volumeTextWidth = MeasureText(GetFonts().small, 15.0f, volumeText);
    draw->AddText(GetFonts().small, 15.0f, abs(rightLeft + 24.0f + 110.0f + 6.0f + 34.0f - volumeTextWidth, rightCenterY - 5.0f),
                  Colors::Text3, volumeText.c_str());
    // 滚轮微调音量：鼠标在音量组（图标+滑条+百分比）上时，滚轮 ±2%
    {
        const ImVec2 volumeMin = abs(rightLeft - 2.0f, rightCenterY - 16.0f);
        const ImVec2 volumeMax = abs(rightLeft + 174.0f, rightCenterY + 16.0f);
        if (ImGui::IsMouseHoveringRect(volumeMin, volumeMax)) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) actions->volumeWheelDelta = wheel;
        }
    }

    ImGui::SetCursorPos(ImVec2(rightLeft + 182.0f, rightCenterY - 14.0f));
    if (IconButton("output_device", Icons::Headset, 28.0f, 16.0f, Colors::Text2, false, true, "输出设备（M4）")) {
        actions->openOutputDevice = true;
    }
    ImGui::SetCursorPos(ImVec2(rightLeft + 220.0f, rightCenterY - 14.0f));
    if (IconButton("sleep_timer", Icons::Bedtime, 28.0f, 16.0f, Colors::Text2, false, true, "定时关闭（M4）")) {
        actions->openSleepTimer = true;
    }
    ImGui::SetCursorPos(ImVec2(rightLeft + 258.0f, rightCenterY - 14.0f));
    if (IconButton("queue", Icons::PlaylistPlay, 28.0f, 16.0f, Colors::Text2, false, true, "播放队列（M4）")) {
        actions->openQueue = true;
    }
}

}  // namespace Ui