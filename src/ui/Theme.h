#pragma once

#include <cstdint>
#include <string>

#include <imgui.h>

// 设计 token：与 Java 版 BmTheme.kt 一一对应（三档背景 + 三档文字 + 品牌红）。
namespace Ui {

namespace Colors {
inline constexpr ImU32 Bg = IM_COL32(0x18, 0x18, 0x1B, 255);
inline constexpr ImU32 BgSide = IM_COL32(0x12, 0x12, 0x14, 255);
inline constexpr ImU32 BgPlayer = IM_COL32(0x0F, 0x0F, 0x11, 255);
inline constexpr ImU32 BgTitleBar = IM_COL32(0x0C, 0x0C, 0x0E, 255);
inline constexpr ImU32 Hover = IM_COL32(0x23, 0x23, 0x28, 255);
inline constexpr ImU32 Active = IM_COL32(0x2A, 0x2A, 0x31, 255);
inline constexpr ImU32 Divider = IM_COL32(0x28, 0x28, 0x2E, 255);
inline constexpr ImU32 Text = IM_COL32(0xF2, 0xF2, 0xF4, 255);
inline constexpr ImU32 Text2 = IM_COL32(0xA6, 0xA6, 0xAE, 255);
inline constexpr ImU32 Text3 = IM_COL32(0x6B, 0x6B, 0x75, 255);
inline constexpr ImU32 Brand = IM_COL32(0xEC, 0x41, 0x41, 255);
inline constexpr ImU32 BrandHover = IM_COL32(0xF0, 0x52, 0x52, 255);
inline constexpr ImU32 BrandSoft = IM_COL32(0xEC, 0x41, 0x41, 26);
inline constexpr ImU32 TagBg = IM_COL32(0x22, 0x22, 0x27, 255);
inline constexpr ImU32 Dialog = IM_COL32(0x20, 0x20, 0x24, 255);
inline constexpr ImU32 TrackBar = IM_COL32(0x33, 0x33, 0x3A, 255);
inline constexpr ImU32 White = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 WindowBtnHover = IM_COL32(0x2A, 0x2A, 0x31, 255);
inline constexpr ImU32 WindowCloseHover = IM_COL32(0xC4, 0x2B, 0x1C, 255);
inline constexpr ImU32 WindowBtnIcon = IM_COL32(0xD0, 0xD0, 0xD6, 255);
}  // namespace Colors

// 尺寸 token（Java 版是 dp，这里桌面端按 1dp = 1px 处理）
namespace Dimens {
inline constexpr float TitleBarHeight = 36.0f;
inline constexpr float TitleBarButtonWidth = 46.0f;
inline constexpr float SidebarWidth = 240.0f;
inline constexpr float PlayerHeight = 72.0f;
inline constexpr float CoverBig = 196.0f;
inline constexpr float CoverSide = 36.0f;
inline constexpr float CoverPlayer = 50.0f;
inline constexpr float RowHeight = 44.0f;
inline constexpr float TableHeaderHeight = 34.0f;
inline constexpr float ContentPaddingX = 34.0f;
inline constexpr float GapXs = 4.0f;
inline constexpr float GapSm = 8.0f;
inline constexpr float GapMd = 14.0f;
inline constexpr float GapLg = 24.0f;
inline constexpr float RadiusCard = 14.0f;
inline constexpr float RadiusCoverSm = 7.0f;
inline constexpr float RadiusRow = 8.0f;
inline constexpr float RadiusTag = 4.0f;
}  // namespace Dimens

// 字号（比 Java 版整体调大一档：正文 16、次要 13、标题 20、大标题 34）
namespace FontSize {
inline constexpr float Body = 16.0f;
inline constexpr float Small = 13.0f;
inline constexpr float Title = 20.0f;
inline constexpr float Xl = 34.0f;
inline constexpr float IconBig = 22.0f;
}  // namespace FontSize

struct Fonts {
    ImFont* body = nullptr;      // 16px 正文 + 图标
    ImFont* small = nullptr;     // 13px 次要信息 + 图标
    ImFont* title = nullptr;     // 20px 标题 + 图标
    ImFont* xl = nullptr;        // 34px 大标题
    ImFont* iconBig = nullptr;   // 22px 纯图标（播放/暂停按钮）
};

const Fonts& GetFonts();

// 在 ImGui 上下文创建之后调用一次
void InitFonts();
void InitStyle();

inline ImVec4 Vec4(ImU32 color) { return ImGui::ColorConvertU32ToFloat4(color); }

}  // namespace Ui
