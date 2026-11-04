#include "ui/Theme.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "ui/Icons.h"

namespace Ui {

namespace {

Fonts g_fonts;
bool g_fontsInitialized = false;

std::wstring exeDir() {
    wchar_t buffer[MAX_PATH * 4] = {};
    DWORD len = GetModuleFileNameW(nullptr, buffer, (DWORD)(sizeof(buffer) / sizeof(wchar_t)));
    if (len == 0) return L".";
    std::wstring path(buffer, len);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

// 运行时查找资源文件：先看 exe 旁边的 assets/，再看开发目录 third_party/icons
std::string findAsset(const std::wstring& relative) {
    std::vector<std::wstring> candidates;
    candidates.push_back(exeDir() + L"\\assets\\" + relative);
    candidates.push_back(exeDir() + L"\\..\\third_party\\icons\\" + relative);
    candidates.push_back(exeDir() + L"\\third_party\\icons\\" + relative);
    for (const auto& candidate : candidates) {
        DWORD attrs = GetFileAttributesW(candidate.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) continue;
        int size = WideCharToMultiByte(CP_UTF8, 0, candidate.c_str(), (int)candidate.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8((size_t)size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, candidate.c_str(), (int)candidate.size(), utf8.data(), size, nullptr, nullptr);
        return utf8;
    }
    return {};
}

std::string findChineseFont() {
    const char* candidates[] = {
    // 优先用微软雅黑 Bold（用户反馈字体偏细）
        "C:\\\\Windows\\\\Fonts\\\\msyhbd.ttc",
        "C:\\\\Windows\\\\Fonts\\\\msyhbd.ttf",
        "C:\\\\Windows\\\\Fonts\\\\msyh.ttc",
        "C:\\\\Windows\\\\Fonts\\\\msyh.ttf",
        "C:\\\\Windows\\\\Fonts\\\\simhei.ttf",
    };
    for (const char* path : candidates) {
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return {};
}

void addFontWithIcons(ImFont*& target, const std::string& cjkPath, const std::string& iconPath,
                      float size, bool chinese) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.FontNo = 0;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.PixelSnapH = true;

    // 中文字形范围 + 界面里会用到的小符号（♪ ♥ · …）。ImGui 默认的中文范围不含 U+266A，
    // 不补的话 emoji 占位符会渲染成「?」。
    static ImVector<ImWchar> chineseRanges;
    if (chineseRanges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddChar(0x266A);  // ♪
        builder.AddChar(0x2665);  // ♥
        builder.AddChar(0x00B7);  // ·
        builder.AddChar(0x2026);  // …
        builder.BuildRanges(&chineseRanges);
    }
    const ImWchar* ranges = chinese ? chineseRanges.Data : nullptr;
    if (!cjkPath.empty()) {
        target = io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), size, &config, ranges);
    } else {
        target = io.Fonts->AddFontDefault();
    }
    if (target == nullptr) {
        target = io.Fonts->AddFontDefault();
    }
    if (!iconPath.empty()) {
        static const ImWchar iconRanges[] = { 0xE000, 0xF8FF, 0 };
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.GlyphMinAdvanceX = size;
        iconConfig.PixelSnapH = true;
        iconConfig.OversampleH = 1;
        iconConfig.OversampleV = 1;
        io.Fonts->AddFontFromFileTTF(iconPath.c_str(), size, &iconConfig, iconRanges);
    }
}

}  // namespace

const Fonts& GetFonts() {
    if (!g_fontsInitialized) {
        InitFonts();
    }
    return g_fonts;
}

void InitFonts() {
    if (g_fontsInitialized) return;
    g_fontsInitialized = true;

    const std::string cjkPath = findChineseFont();
    const std::string iconPath = findAsset(L"MaterialIcons-Regular.ttf");

    addFontWithIcons(g_fonts.body, cjkPath, iconPath, FontSize::Body, true);
    addFontWithIcons(g_fonts.small, cjkPath, iconPath, FontSize::Small, true);
    addFontWithIcons(g_fonts.title, cjkPath, iconPath, FontSize::Title, true);
    addFontWithIcons(g_fonts.xl, cjkPath, iconPath, FontSize::Xl, false);
    addFontWithIcons(g_fonts.iconBig, iconPath, iconPath, FontSize::IconBig, false);
}

void InitStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(0, 0);
    style.ChildBorderSize = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(0, 0);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.FramePadding = ImVec2(10, 5);
    style.ScrollbarSize = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = Vec4(Colors::Text);
    colors[ImGuiCol_TextDisabled] = Vec4(Colors::Text3);
    colors[ImGuiCol_WindowBg] = Vec4(Colors::Bg);
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = Vec4(Colors::Dialog);
    colors[ImGuiCol_Border] = Vec4(Colors::Divider);
    colors[ImGuiCol_FrameBg] = Vec4(Colors::Active);
    colors[ImGuiCol_FrameBgHovered] = Vec4(Colors::Hover);
    colors[ImGuiCol_FrameBgActive] = Vec4(Colors::Active);
    colors[ImGuiCol_TitleBg] = Vec4(Colors::BgTitleBar);
    colors[ImGuiCol_TitleBgActive] = Vec4(Colors::BgTitleBar);
    colors[ImGuiCol_MenuBarBg] = Vec4(Colors::BgTitleBar);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = Vec4(Colors::Active);
    colors[ImGuiCol_ScrollbarGrabHovered] = Vec4(Colors::Hover);
    colors[ImGuiCol_ScrollbarGrabActive] = Vec4(Colors::Brand);
    colors[ImGuiCol_Button] = Vec4(Colors::Hover);
    colors[ImGuiCol_ButtonHovered] = Vec4(Colors::Active);
    colors[ImGuiCol_ButtonActive] = Vec4(Colors::Active);
    colors[ImGuiCol_Header] = Vec4(Colors::Hover);
    colors[ImGuiCol_HeaderHovered] = Vec4(Colors::Active);
    colors[ImGuiCol_HeaderActive] = Vec4(Colors::Active);
    colors[ImGuiCol_Separator] = Vec4(Colors::Divider);
    colors[ImGuiCol_SeparatorHovered] = Vec4(Colors::Brand);
    colors[ImGuiCol_SeparatorActive] = Vec4(Colors::Brand);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.93f, 0.25f, 0.25f, 0.35f);
}

}  // namespace Ui
