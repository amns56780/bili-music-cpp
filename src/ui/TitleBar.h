#pragma once

struct GLFWwindow;

namespace Ui {

// 自绘标题栏（无边框窗口）：拖动、双击最大化、最小化/最大化/关闭按钮。
// 尺寸/配色与 Java 版 TitleBar.kt 对应，窗口控制按钮用线段绘制（不依赖图标字体）。
// 返回 true 表示用户点了左侧「折叠/展开侧栏」按钮
bool DrawTitleBar(GLFWwindow* window, bool maximized, bool sidebarCollapsed);

}  // namespace Ui
