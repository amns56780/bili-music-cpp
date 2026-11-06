#pragma once

#include <string>
#include <vector>

namespace util {

// 歌单分享链接（与 Java 版 PlaylistShareCodec 完全同一协议）：
//   bm://pl/v1/<base64url(deflate(JSON))>
//   JSON: {"v":1,"t":"歌单标题","s":["BV...","BV..."]}
// 不依赖服务器：歌单内容全在链接里，另一端解出 bvid 后逐首取视频信息落库。
struct SharedPlaylist {
    int version = 1;
    std::string title;
    std::vector<std::string> bvids;
};

constexpr const char* kShareLinkPrefix = "bm://pl/v1/";
constexpr size_t kShareLinkMaxSongs = 300;

bool IsShareLink(const std::string& text);
std::string EncodeShareLink(const std::string& title, const std::vector<std::string>& bvids);
bool DecodeShareLink(const std::string& text, SharedPlaylist* out);

}  // namespace util
