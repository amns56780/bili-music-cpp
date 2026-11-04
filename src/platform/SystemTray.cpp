#include "platform/SystemTray.h"

#include <windows.h>
#include <shellapi.h>

#include <cstring>

#include "util/AppLog.h"

namespace platform {

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 101;
constexpr UINT kMenuShow = 1;
constexpr UINT kMenuTogglePlay = 2;
constexpr UINT kMenuNext = 3;
constexpr UINT kMenuPrevious = 4;
constexpr UINT kMenuExit = 5;

constexpr int kHotkeyPlayPause = 1;
constexpr int kHotkeyNext = 2;
constexpr int kHotkeyPrevious = 3;
constexpr int kHotkeyStop = 4;
constexpr UINT kModNoRepeat = 0x4000;

const wchar_t* kWindowClass = L"BiliMusicTrayHost";

SystemTray* gActiveTray = nullptr;

}  // namespace

SystemTray::~SystemTray() { shutdown(); }

bool SystemTray::init(const std::string& tooltip, TrayCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    gActiveTray = this;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &SystemTray::windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        AppLog::warn("Tray", "注册托盘窗口类失败");
        return false;
    }
    hwnd_ = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
    if (hwnd_ == nullptr) {
        AppLog::warn("Tray", "创建托盘消息窗口失败");
        return false;
    }

    // exe 资源里的应用图标（app.rc 里第一个 ICON）
    icon_ = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32,
                              LR_SHARED);
    if (icon_ == nullptr) icon_ = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = icon_;
    const std::wstring wideTooltip(tooltip.begin(), tooltip.end());
    wcsncpy_s(data.szTip, std::size(data.szTip), wideTooltip.c_str(), _TRUNCATE);
    iconAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);

    // 全局键盘钩子：媒体键 / Ctrl+Alt+Space/←/→ 都能用，
    // 不依赖 RegisterHotKey（那个容易被其它播放器/系统媒体控制占用而注册失败）。
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &SystemTray::keyboardProc,
                                      GetModuleHandleW(nullptr), 0);
    if (keyboardHook_ == nullptr) {
        AppLog::warn("Tray", "安装全局键盘钩子失败（快捷键不可用）");
    }
    AppLog::info("Tray", std::string("系统托盘初始化完成 icon=") + (iconAdded_ ? "ok" : "fail") +
                            " hotkey=" + (keyboardHook_ != nullptr ? "ok" : "fail"));
    return true;
}

void SystemTray::shutdown() {
    if (keyboardHook_ != nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (hwnd_ != nullptr) {
        if (iconAdded_) {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = hwnd_;
            data.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &data);
            iconAdded_ = false;
        }
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (gActiveTray == this) gActiveTray = nullptr;
}

void SystemTray::showContextMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, kMenuShow, L"显示窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuTogglePlay, L"播放 / 暂停");
    AppendMenuW(menu, MF_STRING, kMenuPrevious, L"上一首");
    AppendMenuW(menu, MF_STRING, kMenuNext, L"下一首");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd_);
    const UINT command = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x,
                                              cursor.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command != 0) dispatch((int)command);
}

void SystemTray::dispatch(int command) {
    switch (command) {
        case kMenuShow:
            if (callbacks_.onShowWindow) callbacks_.onShowWindow();
            break;
        case kMenuTogglePlay:
            if (callbacks_.onTogglePlay) callbacks_.onTogglePlay();
            break;
        case kMenuNext:
            if (callbacks_.onNext) callbacks_.onNext();
            break;
        case kMenuPrevious:
            if (callbacks_.onPrevious) callbacks_.onPrevious();
            break;
        case kMenuExit:
            if (callbacks_.onExit) callbacks_.onExit();
            break;
        default:
            break;
    }
}

LRESULT CALLBACK SystemTray::keyboardProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        SystemTray* tray = gActiveTray;
        const KBDLLHOOKSTRUCT* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if (tray != nullptr && info != nullptr &&
            (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)) {
            switch (info->vkCode) {
                case VK_MEDIA_PLAY_PAUSE:
                case VK_MEDIA_STOP:
                    tray->dispatch(kMenuTogglePlay);
                    return 1;
                case VK_MEDIA_NEXT_TRACK:
                    tray->dispatch(kMenuNext);
                    return 1;
                case VK_MEDIA_PREV_TRACK:
                    tray->dispatch(kMenuPrevious);
                    return 1;
                default:
                    break;
            }
            const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (ctrlDown && altDown) {
                switch (info->vkCode) {
                    case 'H':
                        tray->dispatch(kMenuShow);
                        return 1;
                    case VK_SPACE:
                        tray->dispatch(kMenuTogglePlay);
                        return 1;
                    case VK_LEFT:
                        tray->dispatch(kMenuPrevious);
                        return 1;
                    case VK_RIGHT:
                        tray->dispatch(kMenuNext);
                        return 1;
                    default:
                        break;
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK SystemTray::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    SystemTray* tray = gActiveTray;
    switch (message) {
        case kTrayCallbackMessage:
            if (tray != nullptr) {
                const UINT event = LOWORD(lparam);
                if (event == WM_LBUTTONDBLCLK || event == NIN_SELECT) {
                    tray->dispatch(kMenuShow);
                } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                    tray->showContextMenu();
                }
            }
            return 0;
        case WM_HOTKEY:
            if (tray != nullptr) {
                switch ((int)wparam) {
                    case kHotkeyPlayPause:
                        tray->dispatch(kMenuTogglePlay);
                        break;
                    case kHotkeyNext:
                        tray->dispatch(kMenuNext);
                        break;
                    case kHotkeyPrevious:
                        tray->dispatch(kMenuPrevious);
                        break;
                    case kHotkeyStop:
                        tray->dispatch(kMenuTogglePlay);
                        break;
                    default:
                        break;
                }
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace platform
