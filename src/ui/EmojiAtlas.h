#pragma once

#include <cstdint>
#include <string>

namespace Ui {

// 彩色 emoji（Segoe UI Emoji 通过 Direct2D 颜色字体渲染成 GL 纹理）
// 文本里的 emoji 会被拆成独立 token，由 drawRichText 用纹理绘制。
bool IsEmojiCodepoint(uint32_t codepoint);
// 从 text[offset] 取一个 emoji token（含基字符、变体选择符、肤色、ZWJ 序列等）。
// 不是 emoji 时返回 0。
size_t NextEmojiToken(const std::string& text, size_t offset, std::string* token);
// 返回 emoji 纹理（GLuint，0 表示渲染失败），缓存。
unsigned int EmojiTexture(const std::string& token);

}  // namespace Ui
