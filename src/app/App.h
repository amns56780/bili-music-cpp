#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
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

    // M4：托盘 / 全局媒体快捷键用
    void togglePlayback();
    void playNext();
    void playPrevious();

    // 防熄屏用：当前是否在出声
    bool playbackActive() const { return playing_ && hasMedia_; }

    // 播放/进度乐观 UI 用（减少按钮与进度条的延迟感）
    void requestSeekPreview(double positionMs);
    void seekRelative(double seconds);

    // 关闭窗口（收进托盘）时也保存窗口位置/大小，下次启动沿用
    void saveWindowState();

private:
    struct AsyncEvent {
        enum class Kind {
            Login,
            Play,
            BvPlay,
            QrGenerate,
            QrPoll,
            FavFolders,
            ImportProgress,
            ImportDone,
            DownloadDone,
            LocalScanDone,
            Prefetched
        };
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
        int requestedQualityId = -1;
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
        // M3 歌单导入
        std::vector<bili::FavFolder> favFolders;
        bili::ImportedSongsResult imported;
        std::string progressText;
        int progressCurrent = 0;
        int progressTotal = 0;
        bool overwrite = false;
        std::string sourceType;
        std::string sourceTitle;
        std::string desiredTitle;
        std::string filePath;
        std::vector<std::string> localPaths;
    };

    // 重名对话框后续动作
    enum class ConflictAction { None, Create, Rename, ImportFav, ImportToView, ImportPending };
    struct ImportContext {
        std::string title;
        std::string sourceType;
        std::string sourceTitle;
        long long folderId = 0;
        long long spaceMid = 0;
        long long seasonId = 0;
        bool toView = false;
        bool overwrite = false;
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

    // M3：歌单管理 / 导入
    void openCreatePlaylistDialog();
    void openRenamePlaylistDialog();
    void openDeletePlaylistDialog();
    void handleNameDialogConfirm();
    void handleConflictChoice(int choice);  // 0=覆盖 1=自动重命名 2=取消
    void beginImport(const ImportContext& context);
    void startImportWorker(const ImportContext& context);
    void loadFavFolders();
    void drawM3Dialogs();
    void finishImport(const AsyncEvent& event);
    void applyMultiAction(long long targetPlaylistId);
    void chooseLocalCover();

    // M4：定时关闭 / 播放队列 / 输出设备 / 沉浸模式
    void startSleepTimer(int minutes);
    void cancelSleepTimer();
    void checkSleepTimer();
    void refreshOutputDevices();
    void applyOutputDevice(const std::string& id);
    void openImmersive();
    void closeImmersive();
    void toggleImmersiveFullscreen();
    void pickImmersiveImage();
    void drawImmersive();
    void startDownloadCurrent();
    void prefetchNextSong();
    void scanLocalMusic();
    void finishDownload(const AsyncEvent& event);
    void finishLocalScan(const AsyncEvent& event);

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
    std::string curStreamUrl_;
    long long curCid_ = 0;

    // 已解析的流地址缓存（省掉重复取流的 1s 左右；也用于预取下一首）
    struct CachedStream {
        std::string url;
        int requestedQualityId = 0;  // 请求的档位（0=自动），用于判断缓存是否匹配当前音质设置
        int qualityId = 0;           // 实际返回的档位（显示用）
        std::vector<std::string> backupUrls;
        int64_t deadlineEpochSeconds = 0;
    };
    std::map<long long, CachedStream> streamCache_;
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

    // ---------------- M3 歌单管理 / 导入 ----------------
    bool nameDialogRequested_ = false;
    bool nameDialogIsRename_ = false;
    long long nameDialogPlaylistId_ = -1;
    char nameInput_[256] = {};
    std::string nameDialogError_;

    bool conflictDialogRequested_ = false;
    PlaylistNameConflict conflict_;
    ConflictAction conflictAction_ = ConflictAction::None;
    long long conflictPlaylistId_ = -1;

    bool deleteDialogRequested_ = false;
    long long deletePlaylistId_ = -1;

    bool importSourceDialogRequested_ = false;
    bool favFolderDialogRequested_ = false;
    bool favFoldersLoading_ = false;
    std::vector<bili::FavFolder> favFolders_;
    bool importRunning_ = false;
    std::string importProgressText_;

    ImportContext pendingImport_;
    bool pendingImportValid_ = false;

    // UP主/合集导入在拿到真实标题后才判重，冲突时把数据暂存这里
    bili::ImportedSongsResult pendingImportedResult_;
    std::string pendingImportedSourceType_;
    std::string pendingImportedTitle_;
    bool pendingImportedValid_ = false;

    // UP主 UID / 合集 ID 输入弹窗
    bool importInputRequested_ = false;
    int importInputKind_ = 0;  // 1=UP主投稿 2=合集
    char importInput_[512] = {};
    std::string importInputError_;

    // 多选 / 封面（M3 第三批）
    bool selectMode_ = false;
    std::set<long long> selectedSongIds_;
    int multiAction_ = 0;  // 1=复制 2=移动
    bool targetPlaylistDialogRequested_ = false;
    bool deleteSongsDialogRequested_ = false;
    bool coverSongDialogRequested_ = false;

    // M4
    bool sleepDialogRequested_ = false;
    bool sleepTimerActive_ = false;
    uint64_t sleepTimerEndTick_ = 0;
    int sleepTimerMinutes_ = 0;
    bool queueDialogRequested_ = false;
    bool outputDeviceDialogRequested_ = false;
    std::vector<std::pair<std::string, std::string>> outputDevices_;  // id, 名称
    std::string selectedOutputDeviceId_;
    // 沉浸模式（对齐 Java Immersive.kt：点击播放条封面自下而上滑入，背景默认桌面壁纸模糊）
    // 乐观 UI：暂停/播放后短暂不信任引擎状态；seek 后先用目标位置显示，引擎追上再切回
    uint64_t playbackOverrideUntil_ = 0;
    double seekPreviewMs_ = -1.0;
    uint64_t seekPreviewUntil_ = 0;

    // 播放缓存范围（秒）：2/5/10/20，写入 audio.network_cache
    int networkCacheSeconds_ = 5;
    bool cacheSettingsDialogRequested_ = false;

    bool immersiveOpen_ = false;
    bool immersiveClosing_ = false;
    float immersiveAnim_ = 0.0f;
    bool immersiveFullscreen_ = false;
    int immersiveBgMode_ = 0;  // 0=桌面壁纸 1=自定义图片 2=纯色
    std::string immersiveBgPath_;
    bool immersiveShowWave_ = true;
    bool immersiveVolumeOpen_ = false;
    bool immersiveBgMenuOpen_ = false;
    uint64_t immersiveEnteredTick_ = 0;
    std::string immersiveModeTip_;
    uint64_t immersiveModeTipUntil_ = 0;
    int savedWindowX_ = 0;
    int savedWindowY_ = 0;
    int savedWindowW_ = 1240;
    int savedWindowH_ = 860;
};
