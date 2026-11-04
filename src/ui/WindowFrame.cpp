#include "ui/WindowFrame.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Ui {

namespace {

WNDPROC g_oldProc = nullptr;

constexpr int kResizeBorder = 6;
constexpr int kTitleBarHeight = 36;
constexpr int kTitleBarButtonsWidth = 46 * 3;

LRESULT CALLBACK WindowFrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wParam != 0) return 0;  // 客户区铺满整个窗口（去掉 WS_THICKFRAME 的可视边框）
            break;
        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const bool maximized = IsZoomed(hwnd) != 0;
            if (!maximized) {
                const bool left = pt.x < kResizeBorder;
                const bool right = pt.x >= rc.right - kResizeBorder;
                const bool top = pt.y < kResizeBorder;
                const bool bottom = pt.y >= rc.bottom - kResizeBorder;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            // 标题栏最左侧是「折叠/展开侧栏」按钮，需要接收点击
            if (pt.y < kTitleBarHeight && pt.x >= 0 && pt.x < 36) return HTCLIENT;
            // 标题栏（右侧三个窗口按钮除外）交给系统做原生拖动/双击最大化
            if (pt.y < kTitleBarHeight && pt.x < rc.right - kTitleBarButtonsWidth) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            // 先让 GLFW 处理（它会写入最小/最大尺寸限制），再按显示器工作区修正最大化位置/尺寸
            LRESULT result = g_oldProc != nullptr
                                 ? CallWindowProcW(g_oldProc, hwnd, msg, wParam, lParam)
                                 : 0;
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(monitor, &monitorInfo)) {
                info->ptMaxPosition.x = monitorInfo.rcWork.left - monitorInfo.rcMonitor.left;
                info->ptMaxPosition.y = monitorInfo.rcWork.top - monitorInfo.rcMonitor.top;
                info->ptMaxSize.x = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
                info->ptMaxSize.y = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
            }
            return result;
        }
        default:
            break;
    }
    if (g_oldProc != nullptr) return CallWindowProcW(g_oldProc, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

void InstallWindowFrame(GLFWwindow* window) {
    if (window == nullptr) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) return;

    // thickframe 让四边/四角可缩放；配合 WM_NCCALCSIZE 去掉可视边框；
    // MAXIMIZEBOX/MINIMIZEBOX/SYSMENU 让系统双击标题栏能最大化、任务栏行为正常。
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    g_oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowFrameProc)));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool WindowIsMaximized(GLFWwindow* window) {
    if (window == nullptr) return false;
    HWND hwnd = glfwGetWin32Window(window);
    return hwnd != nullptr && IsZoomed(hwnd) != 0;
}

}  // namespace Ui
