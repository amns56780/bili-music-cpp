#pragma once

#include <windows.h>

#include <functional>
#include <string>

// 系统托盘 + 全局媒体快捷键（M4）。
// 用一个隐藏的 message-only 窗口接收托盘回调 / WM_HOTKEY，
// 不干扰 GLFW 自己的消息循环（glfwPollEvents 会泵线程消息队列）。
namespace platform {

struct TrayCallbacks {
    std::function<void()> onShowWindow;
    std::function<void()> onTogglePlay;
    std::function<void()> onNext;
    std::function<void()> onPrevious;
    std::function<void()> onExit;
};

class SystemTray {
public:
    SystemTray() = default;
    ~SystemTray();
    SystemTray(const SystemTray&) = delete;
    SystemTray& operator=(const SystemTray&) = delete;

    bool init(const std::string& tooltip, TrayCallbacks callbacks);
    void shutdown();

    bool active() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void showContextMenu();
    void dispatch(int command);

    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    TrayCallbacks callbacks_;
    bool iconAdded_ = false;
    bool hotkeysRegistered_ = false;
};

}  // namespace platform
