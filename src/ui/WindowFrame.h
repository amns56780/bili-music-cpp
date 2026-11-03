#pragma once

struct GLFWwindow;

namespace Ui {

// 无边框窗口的原生边框处理（Win32 子类化 WndProc）：
//   - WM_NCHITTEST：标题栏返回 HTCAPTION -> 系统原生拖动（零卡顿，不经过 UI 线程）
//     ；四边/四角返回 HTLEFT/HTTOPLEFT... -> 系统原生缩放
//   - WM_NCCALCSIZE：去掉 WS_THICKFRAME 自带的可视边框，客户区铺满
//   - WM_GETMINMAXINFO：最大化时按工作区收住，不盖住任务栏
// 这是 Java 版做不到的（AWT 的窗口过程挡在中间，消息进不了 JVM）。
void InstallWindowFrame(GLFWwindow* window);

bool WindowIsMaximized(GLFWwindow* window);

}  // namespace Ui
