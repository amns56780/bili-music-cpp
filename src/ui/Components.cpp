#include "ui/Components.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace Ui {

namespace {

struct Gradient {
    ImU32 from;
    ImU32 to;
};

// 与 Java 版 BmColors.gradients 一一对应
constexpr Gradient kGradients[] = {
    { IM_COL32(0xF0, 0xA4, 0x6B, 255), IM_COL32(0xC4, 0x55, 0x3C, 255) },
    { IM_COL32(0x3E, 0x7B, 0xD6, 255), IM_COL32(0x1E, 0x3D, 0x6E, 255) },
    { IM_COL32(0x6B, 0xB6, 0xA0, 255), IM_COL32(0x2C, 0x6A, 0x5A, 255) },
    { IM_COL32(0x9B, 0x7B, 0xD4, 255), IM_COL32(0x5A, 0x3E, 0x8E, 255) },
    { IM_COL32(0xE4, 0xC5, 0x6B, 255), IM_COL32(0xA6, 0x7C, 0x2E, 255) },
    { IM_COL32(0x5D, 0x8C, 0xFF, 255), IM_COL32(0x2B, 0x4C, 0x9E, 255) },
    { IM_COL32(0xE0, 0x6B, 0x8B, 255), IM_COL32(0x8E, 0x33, 0x50, 255) },
    { IM_COL32(0x7F, 0xB0, 0x69, 255), IM_COL32(0x3F, 0x6B, 0x33, 255) },
};
constexpr int kGradientCount = (int)(sizeof(kGradients) / sizeof(kGradients[0]));

uint32_t fnv1a(const std::string& text) {
    uint32_t hash = 2166136261u;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

// 解码一个 UTF-8 码点（返回码点和消耗字节数；非法字节按单字节 ASCII 兜底）
uint32_t decodeUtf8(const std::string& text, size_t offset, size_t* length) {
    const unsigned char lead = (unsigned char)text[offset];
    size_t size = 1;
    uint32_t codepoint = lead;
    if ((lead & 0xE0) == 0xC0) { size = 2; codepoint = lead & 0x1Fu; }
    else if ((lead & 0xF0) == 0xE0) { size = 3; codepoint = lead & 0x0Fu; }
    else if ((lead & 0xF8) == 0xF0) { size = 4; codepoint = lead & 0x07u; }
    if (offset + size > text.size()) size = text.size() - offset;
    for (size_t i = 1; i < size; ++i) {
        const unsigned char continuation = (unsigned char)text[offset + i];
        codepoint = (codepoint << 6) | (uint32_t)(continuation & 0x3Fu);
    }
    *length = size;
    return codepoint;
}

bool isEmojiOrSymbol(uint32_t codepoint) {
    return (codepoint >= 0x1F000 && codepoint <= 0x1FAFF) ||  // emoji
           (codepoint >= 0x2600 && codepoint <= 0x27BF) ||    // 符号 / dingbats
           (codepoint >= 0x2B00 && codepoint <= 0x2BFF);
}

// 变体选择符 / 零宽连接符：本身不可见，直接丢掉，不要顶掉一个音符图标
bool isInvisibleModifier(uint32_t codepoint) {
    return (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || codepoint == 0x200D;
}

// ImGui 用的字体（微软雅黑）没有 emoji 字形，直接渲染会是一串「?」。
// 显示层把 emoji 换成一个音符，数据本身不动（Java 版仍能看到原始 emoji）。
std::string sanitizeForDisplay(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t offset = 0;
    while (offset < text.size()) {
        size_t length = 0;
        const uint32_t codepoint = decodeUtf8(text, offset, &length);
        if (isInvisibleModifier(codepoint)) {
            // 跳过
        } else if (isEmojiOrSymbol(codepoint)) {
            // 用 Material Icons 的音符字形（图标字体合并进了正文/标题字体，肯定有字形）
            out += Icons::Utf8(Icons::MusicNote);
        } else {
            out.append(text, offset, length);
        }
        offset += length;
    }
    return out;
}

// 取 UTF-8 字符串的第一个字符（最多 4 字节），emoji/符号退化成音符
std::string firstUtf8Char(const std::string& text) {
    if (text.empty()) return {};
    size_t length = 0;
    const uint32_t codepoint = decodeUtf8(text, 0, &length);
    if (isEmojiOrSymbol(codepoint) || isInvisibleModifier(codepoint)) {
        return Icons::Utf8(Icons::MusicNote);
    }
    return text.substr(0, length);
}

float clampf(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

ImU32 lerpColor(ImU32 from, ImU32 to, float t) {
    const float ar = (float)((from >> IM_COL32_R_SHIFT) & 0xFF);
    const float ag = (float)((from >> IM_COL32_G_SHIFT) & 0xFF);
    const float ab = (float)((from >> IM_COL32_B_SHIFT) & 0xFF);
    const float br = (float)((to >> IM_COL32_R_SHIFT) & 0xFF);
    const float bg = (float)((to >> IM_COL32_G_SHIFT) & 0xFF);
    const float bb = (float)((to >> IM_COL32_B_SHIFT) & 0xFF);
    return IM_COL32((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t), (int)(ab + (bb - ab) * t), 255);
}

void drawTextAt(ImFont* font, float size, ImVec2 pos, ImU32 color, const std::string& text) {
    ImGui::GetWindowDrawList()->AddText(font, size, pos, color, text.c_str());
}

float textWidth(ImFont* font, float size, const std::string& text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

}  // namespace

void Place(float x, float y) { ImGui::SetCursorPos(ImVec2(x, y)); }

float MeasureText(ImFont* font, float size, const std::string& text) {
    return textWidth(font, size, text);
}

std::string DisplayText(const std::string& text) { return sanitizeForDisplay(text); }

void TextBody(const std::string& text, ImU32 color) {
    ImGui::PushFont(GetFonts().body);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void TextBodySm(const std::string& text, ImU32 color) {
    ImGui::PushFont(GetFonts().small);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void TextLabel(const std::string& text, ImU32 color) {
    ImGui::PushFont(GetFonts().small);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void TextTitle(const std::string& text, ImU32 color) {
    ImGui::PushFont(GetFonts().title);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void TextXl(const std::string& text, ImU32 color) {
    ImGui::PushFont(GetFonts().xl);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void TextNumber(const std::string& text, ImU32 color) {
    ImGui::PushFont(GetFonts().small);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec4(color));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void TextEllipsis(const std::string& rawText, float maxWidth, ImFont* font, ImU32 color, ImVec2 pos) {
    const std::string text = sanitizeForDisplay(rawText);
    ImGui::PushFont(font);
    const float fontSize = ImGui::GetFontSize();
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    if (width <= maxWidth) {
        ImGui::PopFont();
        drawTextAt(font, fontSize, pos, color, text);
        return;
    }
    const std::string ellipsis = "…";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis.c_str()).x;
    // 按 UTF-8 边界线性累加，找最长可显示前缀。
    // （之前用二分 + 「跳过续字节」，当 mid 正好落在续字节上会被退回到 low，
    //   导致 low 永远不变 —— 长标题时会死循环，界面就「未响应」。）
    size_t offset = 0;
    size_t best = 0;
    float lastWidth = 0.0f;
    while (offset < text.size()) {
        size_t length = 1;
        decodeUtf8(text, offset, &length);
        if (length == 0) break;
        const size_t next = offset + length;
        if (next > text.size()) break;
        const float width = ImGui::CalcTextSize(text.substr(0, next).c_str()).x;
        if (width + ellipsisWidth <= maxWidth) {
            best = next;
            lastWidth = width;
            offset = next;
        } else {
            break;
        }
    }
    (void)lastWidth;
    ImGui::PopFont();
    if (best == 0) return;
    drawTextAt(font, fontSize, pos, color, text.substr(0, best) + ellipsis);
}

namespace {

// 图标字体（Material Icons）字形在行框里偏上：视觉中心约在 textTop + 0.325 * iconSize。
// 想让图标和容器同心，textTop 要用 size/2 - 0.325*iconSize，而不是简单的 (box-icon)/2。
float iconTextTopForCenter(float boxSize, float iconSize) {
    return boxSize * 0.5f - iconSize * 0.325f;
}

}  // namespace

bool IconButton(const char* id, uint32_t icon, float size, float iconSize, ImU32 tint,
                bool active, bool enabled, const char* tooltip) {
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##icon", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (tooltip != nullptr && hovered) ImGui::SetTooltip("%s", tooltip);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered && enabled) {
        draw->AddCircleFilled(ImVec2(pos.x + size / 2, pos.y + size / 2), size / 2, Colors::Hover);
    } else if (active) {
        draw->AddCircleFilled(ImVec2(pos.x + size / 2, pos.y + size / 2), size / 2, Colors::Active);
    }
    const ImU32 color = enabled ? tint : IM_COL32(0xA6, 0xA6, 0xAE, 90);
    const std::string glyph = Icons::Utf8(icon);
    const float width = textWidth(GetFonts().body, iconSize, glyph);
    drawTextAt(GetFonts().body, iconSize,
               ImVec2(pos.x + (size - width) / 2, pos.y + iconTextTopForCenter(size, iconSize)), color,
               glyph);
    ImGui::PopID();
    return enabled && pressed;
}

bool PrimaryButton(const char* id, const std::string& label, uint32_t icon, float height) {
    ImGui::PushID(id);
    const ImFont* font = GetFonts().body;
    const std::string glyph = icon != 0 ? Icons::Utf8(icon) : std::string();
    const float iconWidth = glyph.empty() ? 0.0f : textWidth(GetFonts().body, 17.0f, glyph) + 7.0f;
    const float labelWidth = textWidth(GetFonts().body, 18.0f, label);
    const float width = labelWidth + iconWidth + 28.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##primary", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                        hovered ? Colors::BrandHover : Colors::Brand, height / 2);
    float x = pos.x + (width - (labelWidth + iconWidth)) / 2;
    if (!glyph.empty()) {
        drawTextAt(GetFonts().body, 17.0f, ImVec2(x, pos.y + iconTextTopForCenter(height, 17.0f)),
                   Colors::White, glyph);
        x += iconWidth;
    }
    drawTextAt(GetFonts().body, 18.0f, ImVec2(x, pos.y + (height - 18.0f) / 2), Colors::White, label);
    (void)font;
    ImGui::PopID();
    return pressed;
}

bool GhostButton(const char* id, const std::string& label, uint32_t icon, float height) {
    ImGui::PushID(id);
    const std::string glyph = icon != 0 ? Icons::Utf8(icon) : std::string();
    const float iconWidth = glyph.empty() ? 0.0f : textWidth(GetFonts().body, 17.0f, glyph) + 6.0f;
    const float labelWidth = textWidth(GetFonts().body, 18.0f, label);
    const float width = labelWidth + iconWidth + 26.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##ghost", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered) {
        draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), Colors::Hover, height / 2);
    }
    float x = pos.x + (width - (labelWidth + iconWidth)) / 2;
    if (!glyph.empty()) {
        drawTextAt(GetFonts().body, 17.0f, ImVec2(x, pos.y + iconTextTopForCenter(height, 17.0f)),
                   Colors::Text2, glyph);
        x += iconWidth;
    }
    drawTextAt(GetFonts().body, 18.0f, ImVec2(x, pos.y + (height - 18.0f) / 2), Colors::Text2, label);
    ImGui::PopID();
    return pressed;
}

bool SidebarAction(const char* id, uint32_t icon, const std::string& label, float height) {
    ImGui::PushID(id);
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##action", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered) {
        draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), Colors::Hover, 8.0f);
    }
    const std::string glyph = Icons::Utf8(icon);
    const float iconWidth = textWidth(GetFonts().body, 17.0f, glyph);
    drawTextAt(GetFonts().body, 17.0f,
               ImVec2(pos.x + 10.0f, pos.y + iconTextTopForCenter(height, 17.0f)), Colors::Text2, glyph);
    drawTextAt(GetFonts().body, 18.0f,
               ImVec2(pos.x + 10.0f + iconWidth + 10.0f, pos.y + (height - 18.0f) / 2), Colors::Text2, label);
    ImGui::PopID();
    return pressed;
}

bool PlayCircleButton(const char* id, bool playing, float size) {
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##play", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(ImVec2(pos.x + size / 2, pos.y + size / 2), size / 2,
                          hovered ? Colors::BrandHover : Colors::Brand);
    // 播放/暂停直接用几何图形画：字体图标的光学中心受 bearing/ascent 影响，
    // 在圆里总会偏一点（用户反馈的「三角形未居中」），几何图形 100% 同心。
    const ImVec2 center(pos.x + size / 2, pos.y + size / 2);
    if (playing) {
        // 参考 Java 版：38dp 圆里放 20dp 的 Material Pause 图标，
        // 实际字形是两根 ~3.3dp 宽、~11.7dp 高的竖条，间距 ~3.3dp
        const float barWidth = size * 0.087f;
        const float barHeight = size * 0.31f;
        const float gap = size * 0.087f;
        const float rounding = barWidth * 0.45f;
        draw->AddRectFilled(ImVec2(center.x - gap / 2 - barWidth, center.y - barHeight / 2),
                            ImVec2(center.x - gap / 2, center.y + barHeight / 2), Colors::White,
                            rounding);
        draw->AddRectFilled(ImVec2(center.x + gap / 2, center.y - barHeight / 2),
                            ImVec2(center.x + gap / 2 + barWidth, center.y + barHeight / 2),
                            Colors::White, rounding);
    } else {
        // 三角形：对齐 Java 版 20dp PlayArrow 的字形尺寸（约 9.2 x 11.7dp），
        // 重心偏左，视觉上往右挪 ~size*0.025 才像居中
        const float halfHeight = size * 0.155f;
        const float halfWidth = size * 0.121f;
        const float offsetX = size * 0.025f;
        draw->AddTriangleFilled(ImVec2(center.x - halfWidth + offsetX, center.y - halfHeight),
                                ImVec2(center.x - halfWidth + offsetX, center.y + halfHeight),
                                ImVec2(center.x + halfWidth + offsetX, center.y), Colors::White);
    }
    ImGui::PopID();
    return pressed;
}

SliderResult ThinSlider(const char* id, float* value, float width, float height, float barHeight,
                        ImU32 barColor) {
    SliderResult result;
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##slider", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (active) {
        const float ratio = (ImGui::GetIO().MousePos.x - pos.x) / (width > 0 ? width : 1);
        *value = clampf(ratio, 0.0f, 1.0f);
        result.changed = true;
    }
    if (ImGui::IsItemDeactivated()) result.finished = true;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float centerY = pos.y + height / 2;
    const float x1 = pos.x + width;
    draw->AddRectFilled(ImVec2(pos.x, centerY - barHeight / 2), ImVec2(x1, centerY + barHeight / 2),
                        Colors::TrackBar, barHeight / 2);
    const float played = pos.x + width * clampf(*value, 0.0f, 1.0f);
    if (played > pos.x + 0.5f) {
        draw->AddRectFilled(ImVec2(pos.x, centerY - barHeight / 2), ImVec2(played, centerY + barHeight / 2),
                            barColor, barHeight / 2);
    }
    if (hovered || active) {
        draw->AddCircleFilled(ImVec2(played, centerY), 5.0f, Colors::White);
    }
    ImGui::PopID();
    return result;
}

void CoverPlaceholder(const std::string& seed, ImVec2 pos, float size, float radius) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const Gradient& gradient = kGradients[seed.empty() ? 0 : (int)(fnv1a(seed) % (uint32_t)kGradientCount)];
    const float r = std::max(0.0f, std::min(radius, size / 2.0f));

    // ImGui 没有「圆角渐变」API。之前用 1px 横带硬画，圆角两侧是逐行台阶，肉眼可见锯齿。
    // 现在拆成「中间轴对齐的渐变条带 + 四个圆角扇形」：
    //   - 条带只有水平/垂直边，像素对齐，没有锯齿；
    //   - 圆角用 PathArcTo + PathFillConvex，ImGui 的填充带抗锯齿。
    if (r >= size / 2.0f - 0.5f) {
        // 完全圆形（账号头像）：实心圆是抗锯齿的，渐变在这种小尺寸上直接用主色
        draw->AddCircleFilled(ImVec2(pos.x + size / 2.0f, pos.y + size / 2.0f), size / 2.0f,
                              gradient.from, 48);
    } else {
        const int steps = std::max(4, (int)(size / 2.0f));
        const ImU32 topColor = lerpColor(gradient.from, gradient.to, 0.05f);
        const ImU32 bottomColor = lerpColor(gradient.from, gradient.to, 0.95f);

        auto fillBand = [&](float y0, float y1, float x0, float x1, float t) {
            if (y1 <= y0 + 0.1f) return;
            draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1 + 0.5f), lerpColor(gradient.from, gradient.to, t));
        };

        for (int i = 0; i < steps; ++i) {
            const float t0 = (float)i / (float)steps;
            const float t1 = (float)(i + 1) / (float)steps;
            const float y0 = pos.y + size * t0;
            const float y1 = pos.y + size * t1;
            const float t = (t0 + t1) / 2.0f;
            if (y1 <= pos.y + r + 0.5f) {
                fillBand(y0, y1, pos.x + r, pos.x + size - r, t);
            } else if (y0 >= pos.y + size - r - 0.5f) {
                fillBand(y0, y1, pos.x + r, pos.x + size - r, t);
            } else {
                fillBand(y0, y1, pos.x, pos.x + size, t);
            }
        }
        // 四个圆角（注意 ImGui 的 y 轴向下，角度按顺时针算）
        const float pid = 3.14159265f;
        // 圆角要填「四分之一圆」：先压入圆心，再沿弧线打点，PathFillConvex 才会填成扇形
        auto corner = [&](ImVec2 center, float startAngle, float endAngle, ImU32 color) {
            draw->PathLineTo(center);
            draw->PathArcTo(center, r, startAngle, endAngle, 12);
            draw->PathFillConvex(color);
        };
        corner(ImVec2(pos.x + r, pos.y + r), pid, pid * 1.5f, topColor);
        corner(ImVec2(pos.x + size - r, pos.y + r), pid * 1.5f, pid * 2.0f, topColor);
        corner(ImVec2(pos.x + size - r, pos.y + size - r), 0.0f, pid * 0.5f, bottomColor);
        corner(ImVec2(pos.x + r, pos.y + size - r), pid * 0.5f, pid, bottomColor);
    }
    const std::string initial = firstUtf8Char(seed);
    const std::string text = initial.empty() ? "♪" : initial;
    ImFont* font = size >= 80.0f ? GetFonts().xl : GetFonts().body;
    const float fontSize = size >= 80.0f ? size * 0.36f : 14.0f;
    const float width = textWidth(font, fontSize, text);
    drawTextAt(font, fontSize,
               ImVec2(pos.x + (size - width) / 2, pos.y + (size - fontSize) / 2),
               IM_COL32(255, 255, 255, 235), text);
}

void TagChip(const std::string& text, ImVec2 pos) {
    ImFont* font = GetFonts().small;
    const float width = textWidth(font, 15.0f, text);
    const ImVec2 end = ImVec2(pos.x + width + 18.0f, pos.y + 26.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, end, Colors::TagBg, 4.0f);
    drawTextAt(font, 15.0f, ImVec2(pos.x + 9.0f, pos.y + 4.0f), Colors::Text2, text);
}

void DividerLine(float x0, float x1, float y) {
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(x1, y), Colors::Divider, 1.0f);
}

}  // namespace Ui
