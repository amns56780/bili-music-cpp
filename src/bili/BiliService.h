#pragma once

#include <cstdint>
#include <functional>
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

struct QrCodeInfo {
    bool ok = false;
    std::string url;   // 二维码内容（B 站 App 扫这个）
    std::string key;   // qrcode_key，轮询时用
    std::string message;
};

struct QrPollInfo {
    bool ok = false;       // 请求/解析是否成功
    bool success = false;  // 0：登录成功
    bool scanned = false;  // 86090：已扫码，等待手机确认
    bool expired = false;  // 86038：二维码已过期
    std::map<std::string, std::string> cookies;  // 成功时的登录态 Cookie
    std::string message;
};

struct FavFolder {
    int64_t id = 0;
    std::string title;
    int mediaCount = 0;
};

struct ImportedSong {
    std::string bvid;
    std::string title;
    std::string artist;
    std::string coverUrl;
    int64_t cid = 0;
    int64_t durationMs = 0;
};

struct ImportedSongsResult {
    bool ok = false;
    std::vector<ImportedSong> songs;
    std::string message;
    int total = 0;
    std::string suggestedTitle;  // UP 主昵称 / 合集标题（用于建歌单）
};

using ImportProgressFn = std::function<void(const std::string& text, int current, int total)>;

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

    // 扫码登录：generate 拿二维码内容 + key，poll 轮询状态；成功时带上登录 Cookie
    QrCodeInfo qrGenerate();
    QrPollInfo qrPoll(const std::string& qrcodeKey);
    void importCookies(const std::map<std::string, std::string>& cookies);

    // ---------------- 歌单导入（M3） ----------------
    std::vector<FavFolder> favFolders(long long mid, std::string* error);
    ImportedSongsResult importFavFolder(long long mediaId, const ImportProgressFn& progress);
    ImportedSongsResult importToView(const ImportProgressFn& progress);
    ImportedSongsResult importSpaceUploads(long long mid, const ImportProgressFn& progress);
    ImportedSongsResult importSeason(long long seasonId, const ImportProgressFn& progress);
    // 收藏夹/投稿列表不带 cid，逐条用 view 接口补齐（合集/toview 自带 cid 则跳过）
    void resolveCids(std::vector<ImportedSong>* songs, const ImportProgressFn& progress);

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
