#pragma once

#include <cstdint>
#include <string>

#include <imgui.h>

#include "ui/Icons.h"
#include "ui/Theme.h"

// 与 Java 版 BmCommon.kt 对应的可复用控件。
// 约定：所有控件都用「当前位置」（GetCursorScreenPos / SetCursorPos）自己排版，
// 不依赖 ImGui 的自动换行（全局 ItemSpacing 已设为 0），这样页面能精确对齐设计稿。
namespace Ui {

void Place(float x, float y);

void TextBody(const std::string& text, ImU32 color = Colors::Text);
void TextBodySm(const std::string& text, ImU32 color = Colors::Text2);
void TextLabel(const std::string& text, ImU32 color = Colors::Text3);
void TextTitle(const std::string& text, ImU32 color = Colors::Text);
void TextXl(const std::string& text, ImU32 color = Colors::Text);
void TextNumber(const std::string& text, ImU32 color = Colors::Text3);

// 在固定宽度里截断文本（超出加 …）
void TextEllipsis(const std::string& text, float maxWidth, ImFont* font, ImU32 color,
                  ImVec2 pos);

// 文本宽度测量（用于手动排版对齐）
float MeasureText(ImFont* font, float size, const std::string& text);

// 显示层文本净化：把 emoji 替换成音符图标（ImGui 字体没有彩色 emoji 字形）。
// 数据本身不动，只影响显示。
std::string DisplayText(const std::string& text);
// 富文本：emoji 用彩色纹理绘制，普通文字用 ImGui 字体绘制
void DrawRichText(ImFont* font, float size, ImVec2 pos, ImU32 color, const std::string& text);
float MeasureRichText(ImFont* font, float size, const std::string& text);

// 图标按钮：hover 圆形底色 + 手型光标；enabled=false 时半透明且不可点
bool IconButton(const char* id, uint32_t icon, float size = 32.0f, float iconSize = 18.0f,
                ImU32 tint = Colors::Text2, bool active = false, bool enabled = true,
                const char* tooltip = nullptr);

// 品牌红胶囊按钮（「播放全部」）
bool PrimaryButton(const char* id, const std::string& label, uint32_t icon = 0,
                   float height = 34.0f);

// 幽灵按钮（「多选」）
bool GhostButton(const char* id, const std::string& label, uint32_t icon = 0,
                 float height = 34.0f);

// 侧栏底部动作行（本地音乐 / 输出设备 / 导入歌单）
bool SidebarAction(const char* id, uint32_t icon, const std::string& label,
                   float height = 36.0f, const char* tooltip = nullptr);

// 播放/暂停大圆按钮
bool PlayCircleButton(const char* id, bool playing, float size = 38.0f);

struct SliderResult {
    bool changed = false;   // 拖动中（值已更新）
    bool finished = false;  // 松手（此时可以下发 seek / 保存音量）
};

// 细进度条 / 音量条：track 4px，hover 时显示白色圆点（对应 Java 的 BmSlider）
SliderResult ThinSlider(const char* id, float* value, float width, float height = 14.0f,
                        float barHeight = 4.0f, ImU32 barColor = Colors::Brand);

// 封面占位：按 seed 哈希取渐变 + 首字（对应 Java 的 BmCover）
void CoverPlaceholder(const std::string& seed, ImVec2 pos, float size, float radius);

// 封面：优先本地图片（coverPath），否则退化成渐变占位；网络封面（coverUrl）等 M4 再接
void CoverImage(const std::string& seed, ImVec2 pos, float size, float radius,
                const std::string& coverUrl, const std::string& coverPath);

// 小标签（「收藏夹」「手动新建」）
void TagChip(const std::string& text, ImVec2 pos);

void DividerLine(float x0, float x1, float y);

}  // namespace Ui
