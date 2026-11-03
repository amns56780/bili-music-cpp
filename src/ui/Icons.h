#pragma once

#include <cstdint>
#include <string>

// Material Icons（Apache-2.0）码点。
// 字体文件：third_party/icons/MaterialIcons-Regular.ttf（构建时复制到 exe 旁边的 assets/）。
// 用 Icons::Utf8(codepoint) 得到 UTF-8 字符串，配合合并进主字体的图标字形使用。
namespace Icons {

inline constexpr uint32_t PlayArrow = 0xE037;
inline constexpr uint32_t Pause = 0xE034;
inline constexpr uint32_t SkipPrevious = 0xE045;
inline constexpr uint32_t SkipNext = 0xE044;
inline constexpr uint32_t Repeat = 0xE040;
inline constexpr uint32_t RepeatOne = 0xE041;
inline constexpr uint32_t Shuffle = 0xE043;
inline constexpr uint32_t VolumeUp = 0xE050;
inline constexpr uint32_t VolumeDown = 0xE04D;
inline constexpr uint32_t VolumeOff = 0xE04F;
inline constexpr uint32_t QueueMusic = 0xE03D;
inline constexpr uint32_t PlaylistPlay = 0xE05F;
inline constexpr uint32_t Fullscreen = 0xE5D0;
inline constexpr uint32_t FullscreenExit = 0xE5D1;
inline constexpr uint32_t Settings = 0xE8B8;
inline constexpr uint32_t MoreHoriz = 0xE5D3;
inline constexpr uint32_t MoreVert = 0xE5D4;
inline constexpr uint32_t Close = 0xE5CD;
inline constexpr uint32_t Link = 0xE157;
inline constexpr uint32_t Add = 0xE145;
inline constexpr uint32_t FolderOpen = 0xE2C8;
inline constexpr uint32_t Headset = 0xE310;
inline constexpr uint32_t CloudDownload = 0xE2C0;
inline constexpr uint32_t Download = 0xF090;
inline constexpr uint32_t AccountCircle = 0xE853;
inline constexpr uint32_t MusicNote = 0xE405;
inline constexpr uint32_t LibraryMusic = 0xE030;
inline constexpr uint32_t Edit = 0xE3C9;
inline constexpr uint32_t Delete = 0xE872;
inline constexpr uint32_t Image = 0xE3F4;
inline constexpr uint32_t Share = 0xE80D;
inline constexpr uint32_t CheckBox = 0xE834;
inline constexpr uint32_t PlaylistAdd = 0xE03B;
inline constexpr uint32_t Bedtime = 0xEF44;
inline constexpr uint32_t GraphicEq = 0xE1B8;
inline constexpr uint32_t BlurOn = 0xE3A5;
inline constexpr uint32_t Alarm = 0xE855;
inline constexpr uint32_t Tune = 0xE429;

// 码点 -> UTF-8（Material Icons 都在 U+E000..U+F8FF，编码后固定 3 字节）
inline std::string Utf8(uint32_t codepoint) {
    char bytes[4];
    bytes[0] = (char)(0xE0 | (codepoint >> 12));
    bytes[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    bytes[2] = (char)(0x80 | (codepoint & 0x3F));
    bytes[3] = 0;
    return std::string(bytes, 3);
}

}  // namespace Icons
