#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "bili/BiliService.h"
#include "data/LibraryStore.h"
#include "media/VlcEngine.h"

struct GLFWwindow;

// C++ 版桌面端应用层（M2）：UI 模块组合 + 本地/在线播放 + 登录态。
class App {
public:
    void init(GLFWwindow* window);
    void draw();
    void shutdown();

private:
    struct AsyncEvent {
        enum class Kind { Login, Play, BvPlay };
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
};
