#include "ui/TitleBar.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cmath>

#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace Ui {

namespace {

enum class WindowButtonKind { Minimize, Maximize, Restore, Close };

void drawWindowButtonGlyph(ImDrawList* draw, ImVec2 center, WindowButtonKind kind, ImU32 color) {
    const float half = 5.0f;
    const float thickness = 1.0f;
    switch (kind) {
        case WindowButtonKind::Minimize:
            draw->AddLine(ImVec2(center.x - half, center.y), ImVec2(center.x + half, center.y), color, thickness);
            break;
        case WindowButtonKind::Maximize:
            draw->AddRect(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half),
                          color, 0.0f, 0, thickness);
            break;
        case WindowButtonKind::Restore:
            draw->AddRect(ImVec2(center.x - half, center.y - half + 2.5f),
                          ImVec2(center.x + half - 2.5f, center.y + half), color, 0.0f, 0, thickness);
            draw->AddLine(ImVec2(center.x - half + 2.5f, center.y - half),
                          ImVec2(center.x + half, center.y - half), color, thickness);
            draw->AddLine(ImVec2(center.x + half, center.y - half),
                          ImVec2(center.x + half, center.y + half - 2.5f), color, thickness);
            break;
        case WindowButtonKind::Close:
            draw->AddLine(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half),
                          color, thickness);
            draw->AddLine(ImVec2(center.x + half, center.y - half), ImVec2(center.x - half, center.y + half),
                          color, thickness);
            break;
    }
}

}  // namespace

bool DrawTitleBar(GLFWwindow* window, bool maximized, bool sidebarCollapsed) {
    const float height = Dimens::TitleBarHeight;
    const float buttonWidth = Dimens::TitleBarButtonWidth;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), Colors::BgTitleBar);

    // 最左侧：折叠/展开侧栏按钮
    bool sidebarToggleClicked = false;
    {
        const float btnX = origin.x + 6.0f;
        const float btnW = 28.0f;
        ImGui::SetCursorScreenPos(ImVec2(btnX, origin.y));
        ImGui::PushID("sidebar_toggle");
        const bool pressed = ImGui::InvisibleButton("##toggle", ImVec2(btnW, height));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            draw->AddRectFilled(ImVec2(btnX, origin.y + 4.0f),
                                ImVec2(btnX + btnW, origin.y + height - 4.0f),
                                Colors::WindowBtnHover, 6.0f);
        }
        const std::string glyph =
            Icons::Utf8(sidebarCollapsed ? Icons::ChevronRight : Icons::ChevronLeft);
        draw->AddText(GetFonts().body, 18.0f,
                      ImVec2(btnX + btnW * 0.5f - 18.0f * 0.5f,
                             origin.y + height * 0.5f - 18.0f * 0.325f),
                      hovered ? Colors::White : Colors::WindowBtnIcon, glyph.c_str());
        if (pressed) sidebarToggleClicked = true;
    }

    // 左边：小图标 + 标题（和 Java 版一样用 label 字号、次要文字色）
    const float titleX = 40.0f;
    const std::string note = Icons::Utf8(Icons::MusicNote);
    draw->AddText(GetFonts().body, 18.0f,
                  ImVec2(origin.x + titleX, origin.y + height * 0.5f - 18.0f * 0.325f), Colors::Text2,
                  note.c_str());
    draw->AddText(GetFonts().small, 15.0f,
                  ImVec2(origin.x + titleX + 18.0f + 8.0f, origin.y + (height - 15.0f) / 2),
                  Colors::Text3, "bilimusic");

    // 拖动和双击最大化交给系统（WM_NCHITTEST 返回 HTCAPTION，见 WindowFrame.cpp）：
    // 原生 modal 拖动由 DWM 直接搬窗口，UI 线程完全不参与，不会像 Java 版那样一卡一卡。
    // 这里只负责右侧三个窗口按钮（该区域 WM_NCHITTEST 返回 HTCLIENT，仍然走 ImGui 点击）。

    // 右侧：最小化 / 最大化(还原) / 关闭
    const WindowButtonKind kinds[3] = { WindowButtonKind::Minimize,
                                        maximized ? WindowButtonKind::Restore : WindowButtonKind::Maximize,
                                        WindowButtonKind::Close };
    for (int i = 0; i < 3; ++i) {
        const WindowButtonKind kind = kinds[i];
        const float x = origin.x + width - buttonWidth * (3 - i);
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##btn", ImVec2(buttonWidth, height));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const bool isClose = kind == WindowButtonKind::Close;
        if (hovered) {
            draw->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + buttonWidth, origin.y + height),
                                isClose ? Colors::WindowCloseHover : Colors::WindowBtnHover);
        }
        const ImU32 tint = (hovered && isClose) ? Colors::White : Colors::WindowBtnIcon;
        drawWindowButtonGlyph(draw, ImVec2(x + buttonWidth / 2, origin.y + height / 2), kind, tint);
        if (pressed) {
            // 直接用 Win32：GLFW 的无边框窗口上 glfwMaximizeWindow 行为不稳定
            HWND hwnd = glfwGetWin32Window(window);
            switch (kind) {
                case WindowButtonKind::Minimize: ShowWindow(hwnd, SW_MINIMIZE); break;
                case WindowButtonKind::Maximize: ShowWindow(hwnd, SW_MAXIMIZE); break;
                case WindowButtonKind::Restore: ShowWindow(hwnd, SW_RESTORE); break;
                case WindowButtonKind::Close: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
            }
        }
        ImGui::PopID();
    }

    // 恢复到内容游标（标题栏下面的区域从 y = height 开始）
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    return sidebarToggleClicked;
}

}  // namespace Ui
