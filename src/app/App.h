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
    // 与渲染无关的每帧逻辑：处理异步事件、同步播放状态、定时器、自动切歌。
    // 窗口隐藏到托盘时主循环也会调用它，保证后台播放能自动播下一首。
    void tick();
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
    void toggleSidebar();

    // 关闭窗口（收进托盘）时也保存窗口位置/大小，下次启动沿用
    void saveWindowState();

    // 应用内卸载：写一个延迟批处理（等本进程退出 → 按选择删数据 → 调 uninstall.exe /S）
    // 之后主循环看到 quitRequested() 就真退出，而不是收进托盘
    bool quitRequested() const { return quitRequested_; }
    void performUninstall(bool keepData);
    void openDataFolder();

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
            Prefetched,
            CacheReady
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
    void checkTrackEnd();
    void selectPlaylist(long long playlistId);
    void playSongById(long long songId);
    void playFirstPlayable();
    void stepSong(int direction);
    // 随机播放：洗牌袋（队列索引的排列），点播/切模式时当前曲排第一，
    // 一轮播完把刚播完的排到最后再重洗 —— 对齐 Java 版 shuffleOrder 语义，避免 rand() 连续重复同一首
    void rebuildShuffleOrder(long long firstSongId);
    bool playShuffleStep(int direction);
    // 用户手动点歌（双击曲目 / 队列点选 / 播放全部）：随机模式下把这首排到洗牌顺序第一位
    void playSongFromUser(long long songId);
    // 歌单页「随机播放」：切到随机模式，并从歌单里随机挑第一首（不是固定从第一首开始）
    void shufflePlayPlaylist();
    // 统一切换播放模式（会持久化 desktop.playmode，进入随机时按需建洗牌顺序）
    void setPlayMode(int mode);
    // 输出设备切换 / 卡死恢复：重载当前媒体并回到原位置（VLC 3 只在媒体启动时读输出设备）
    bool reloadCurrentMediaAt(double positionMs, bool resumeIfPlaying);
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

    // 自动切歌防重复：当前曲目已处理过结尾就不再触发
    long long autoNextHandledSongId_ = -1;
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
    // 整首缓存（激进缓存）：后台下载完整音频到 ~/.bilimusic/cache，播本地文件实现秒 seek
    void startCacheDownload(long long songId, const std::string& bvid, long long cid,
                            int qualitySnapshot, const std::string& title, bool manual = false);
    void prefetchNextSongCache();
    void handleCacheReady(const AsyncEvent& event);
    void clearPlaybackCache();
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
    std::set<long long> cachingSongs_;  // 正在下载整首缓存的歌曲 id
    std::vector<std::string> curBackups_;
    int backupIndex_ = 0;
    int64_t curDeadlineEpoch_ = 0;
    int lastStreamQualityId_ = 0;
    double pendingSeekMs_ = -1.0;
    uint64_t stallWatchStartTick_ = 0;
    int stallRetries_ = 0;
    // 播放中假死看门狗（对齐 Java 版：2s 采样，位置连续 12s 不前进就重建并回到原位置）
    uint64_t stallSampleTick_ = 0;
    double stallLastPositionMs_ = 0.0;
    int stallStuckTicks_ = 0;
    int stallRecoverCount_ = 0;
    std::vector<long long> shuffleOrder_;  // 随机播放的洗牌顺序（存 songId）

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
    long long coverSongSelectedId_ = -1;

    // M4
    bool sleepDialogRequested_ = false;
    bool sleepTimerActive_ = false;
    uint64_t sleepTimerEndTick_ = 0;
    int sleepTimerMinutes_ = 0;
    // 定时关闭到点后「等当前曲播完再停」（Java 版默认语义），而不是立刻掐断
    bool stopAfterCurrentTrack_ = false;
    bool queueDialogRequested_ = false;
    bool outputDeviceDialogRequested_ = false;
    std::vector<std::pair<std::string, std::string>> outputDevices_;  // id, 名称
    std::string selectedOutputDeviceId_;
    // 沉浸模式（对齐 Java Immersive.kt：点击播放条封面自下而上滑入，背景默认桌面壁纸模糊）
    // 乐观 UI：暂停/播放后短暂不信任引擎状态；seek 后先用目标位置显示，引擎追上再切回
    uint64_t playbackOverrideUntil_ = 0;
    double seekPreviewMs_ = -1.0;
    uint64_t seekPreviewUntil_ = 0;
    uint64_t seekPreviewStartTick_ = 0;
    bool seekPreviewSlowShown_ = false;
    double seekPreviewOriginMs_ = 0.0;  // seek 前引擎位置（用于检测 seek 是否真的生效）
    int seekRetryCount_ = 0;
    bool seekHardReloadTried_ = false;

    // 播放缓存范围（秒）：2/5/10/20，写入 audio.network_cache
    int networkCacheSeconds_ = 5;
    bool cacheSettingsDialogRequested_ = false;
    // 设置 / 卸载
    bool settingsDialogRequested_ = false;
    bool uninstallDialogRequested_ = false;
    bool uninstallKeepData_ = true;
    bool quitRequested_ = false;
    // 自动整首缓存（含预取下一首），写入 audio.full_cache，默认开启
    bool fullCacheEnabled_ = true;
    // 侧栏底部入口（本地音乐/输出设备/播放缓存/导入歌单）折叠状态
    bool sidebarActionsExpanded_ = true;

    // 左侧栏折叠（带宽度动画，状态持久化）
    bool sidebarCollapsed_ = false;
    float sidebarAnimWidth_ = 240.0f;

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
