#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace bili {

struct NavInfo {
    bool ok = false;
    bool loggedIn = false;
    std::string userName;
    long long userId = 0;
    std::string message;
};

struct VideoInfo {
    bool ok = false;
    std::string bvid;
    long long cid = 0;
    std::string title;
    std::string artist;
    long long durationMs = 0;
    std::string message;
};

struct AudioStreamInfo {
    bool ok = false;
    std::string url;
    std::vector<std::string> backupUrls;
    int qualityId = 0;
    bool downgraded = false;
    int64_t deadlineEpochSeconds = 0;  // URL 里的 deadline 参数（epoch 秒），0 表示未知
    std::string message;
    std::string qualityLabel() const;
};

// B 站 API 侧：Cookie（与 Java 版共用 ~/.bilimusic/settings.json 的 cookie_blob）、
// WBI 签名（nav → mixin_key）、视频信息、取流（wbi/playurl → 备选 playurl）。
// 所有网络方法都是阻塞的，调用方放到后台线程。
class BiliService {
public:
    void init();

    bool hasCookie() const;
    std::string cookieHeader() const;
    bool loggedIn() const;
    const std::string& userName() const;
    long long userId() const;

    // 手动粘贴整段 Cookie；返回解析出的 cookie 数量
    int importCookieText(const std::string& text);
    void clearCookie();
    void applyLoginState(const NavInfo& nav);

    NavInfo fetchNav();
    VideoInfo fetchVideo(const std::string& bvid);
    AudioStreamInfo resolveAudio(const std::string& bvid, long long cid, int qualityId);

    // settings.json 里的其它键（与 Java 版共用），例如 audio_quality
    std::string getSetting(const std::string& key) const;
    bool setSetting(const std::string& key, const std::string& value);

    static std::string parseBvid(const std::string& input);

private:
    bool loadSettings();
    bool saveSettings();
    std::string ensureMixinKey(bool forceRefresh);
    std::map<std::string, std::string> signParams(const std::map<std::string, std::string>& params,
                                                  const std::string& mixinKey) const;

    mutable std::recursive_mutex mutex_;
    std::map<std::string, std::string> values_;   // settings.json 原样保留（含 Java 版写的键）
    std::map<std::string, std::string> cookies_;
    std::string mixinKey_;
    uint64_t mixinFetchedTick_ = 0;
    std::string userName_;
    long long userId_ = 0;
    bool loggedIn_ = false;
    bool initialized_ = false;
};

}  // namespace bili
