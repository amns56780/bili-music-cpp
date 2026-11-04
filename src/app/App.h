#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "bili/BiliService.h"
#include "data/LibraryStore.h"
#include "media/VlcEngine.h"
#include "util/QrCode.h"

struct GLFWwindow;

// C++ 版桌面端应用层（M2）：UI 模块组合 + 本地/在线播放 + 登录态。
class App {
public:
    void init(GLFWwindow* window);
    void draw();
    void shutdown();

private:
    struct AsyncEvent {
        enum class Kind { Login, Play, BvPlay, QrGenerate, QrPoll };
        Kind kind = Kind::Login;
        bool ok = false;
        long long songId = -1;
        long long cid = 0;
        std::string url;
        std::string title;
        std::string artist;
        std::string bvid;
        std::string message;
        int qualityId = 0;
        bool downgraded = false;
        std::vector<std::string> backupUrls;
        int64_t deadlineEpochSeconds = 0;
        // 扫码登录
        std::string qrUrl;
        std::string qrKey;
        int qrStatus = 0;  // 1=已扫码待确认 2=过期 3=成功 0=出错/未知
        std::map<std::string, std::string> qrCookies;
        std::string userName;
        long long userId = 0;
        bool loggedIn = false;
    };

    void syncPlaybackState();
    void selectPlaylist(long long playlistId);
    void playSongById(long long songId);
    void playFirstPlayable();
    void stepSong(int direction);
    void setStatus(const std::string& text, bool isError = false);
    const StoredPlaylist* currentPlaylist() const;
    std::vector<const StoredSong*> currentSongs() const;
    const StoredSong* findSong(long long songId) const;

    void startOnlinePlay(const StoredSong& song);
    void startOnlinePlayFor(long long songId, const std::string& bvid, long long cid,
                            const std::string& title, const std::string& artist);
    void startLoginCheck();
    void startPastePlay(const std::string& text);
    void postAsync(AsyncEvent event);
    void pollAsync();
    void drawDialogs();
    void checkPlaybackStall();
    void applyQualitySelection(int qualityId);

    // 扫码登录
    enum class QrState { Idle, Loading, Waiting, Scanned, Expired, Success };
    void startQrLogin();
    void startQrPolling(const std::string& qrKey);
    void cancelQrLogin();

    GLFWwindow* window_ = nullptr;
    LibraryStore store_;
    VlcEngine engine_;
    bili::BiliService bili_;

    long long selectedPlaylistId_ = -1;
    long long playingSongId_ = -1;
    std::string playingTitle_;
    std::string playingArtist_;
    int volume_ = 80;
    int mode_ = 1;  // 0=顺序 1=列表循环 2=单曲循环 3=随机
    bool importMenuOpen_ = false;

    std::string status_ = "就绪";
    bool statusError_ = false;

    double positionMs_ = 0.0;
    double durationMs_ = 0.0;
    bool playing_ = false;
    bool hasMedia_ = false;

    // 音质档位（0=自动）与在线播放的当前流信息（用于备用地址/过期重取）
    int qualityId_ = 0;
    bool currentIsOnline_ = false;
    std::string curBvid_;
    long long curCid_ = 0;
    std::vector<std::string> curBackups_;
    int backupIndex_ = 0;
    int64_t curDeadlineEpoch_ = 0;
    int lastStreamQualityId_ = 0;
    double pendingSeekMs_ = -1.0;
    uint64_t stallWatchStartTick_ = 0;
    int stallRetries_ = 0;

    // 登录/账号
    std::string accountName_;
    long long accountId_ = 0;
    bool accountLoggedIn_ = false;

    // 异步网络任务（取流/登录都放后台线程，UI 不卡）
    std::mutex asyncMutex_;
    std::vector<AsyncEvent> asyncEvents_;
    long long pendingSongId_ = -1;

    // 弹窗
    bool linkDialogRequested_ = false;
    bool loginDialogRequested_ = false;
    char linkInput_[512] = {};
    char cookieInput_[8192] = {};

    // 扫码登录状态
    QrState qrState_ = QrState::Idle;
    bool loginTabQr_ = true;
    std::string qrKey_;
    std::string qrMessage_;
    util::QrMatrix qrMatrix_;
    std::shared_ptr<std::atomic<bool>> qrCancel_;
    bool qrCloseRequested_ = false;
};
