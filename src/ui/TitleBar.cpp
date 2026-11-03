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

void DrawTitleBar(GLFWwindow* window, bool maximized) {
    const float height = Dimens::TitleBarHeight;
    const float buttonWidth = Dimens::TitleBarButtonWidth;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), Colors::BgTitleBar);

    // 左边：小图标 + 标题（和 Java 版一样用 label 字号、次要文字色）
    const std::string note = Icons::Utf8(Icons::MusicNote);
    draw->AddText(GetFonts().body, 14.0f, ImVec2(origin.x + 12.0f, origin.y + (height - 14.0f) / 2),
                  Colors::Text2, note.c_str());
    draw->AddText(GetFonts().small, 12.0f, ImVec2(origin.x + 12.0f + 16.0f + 8.0f,
                                                  origin.y + (height - 12.0f) / 2),
                  Colors::Text3, "bilimusic");

    // 拖动区：整个标题栏减去右侧三个按钮。
    // 拖动用「屏幕绝对坐标」算位移，不能用 ImGui 的窗口相对坐标（窗口一动就会自激抖动）。
    const float dragWidth = width - buttonWidth * 3.0f;
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##titlebar_drag", ImVec2(dragWidth > 1.0f ? dragWidth : 1.0f, height));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        HWND hwnd = glfwGetWin32Window(window);
        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    }
    static bool dragging = false;
    static POINT dragStartCursor{};
    static int dragStartWindowX = 0;
    static int dragStartWindowY = 0;
    if (ImGui::IsItemActivated()) {
        POINT cursor{};
        GetCursorPos(&cursor);
        dragStartCursor = cursor;
        glfwGetWindowPos(window, &dragStartWindowX, &dragStartWindowY);
        dragging = true;
    }
    if (dragging && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        POINT cursor{};
        GetCursorPos(&cursor);
        glfwSetWindowPos(window, dragStartWindowX + (cursor.x - dragStartCursor.x),
                         dragStartWindowY + (cursor.y - dragStartCursor.y));
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;

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
}

}  // namespace Ui
