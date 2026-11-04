#include "app/App.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <set>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <GLFW/glfw3.h>

#include <commdlg.h>
#include <shlobj.h>

#include "net/HttpClient.h"
#include "ui/Components.h"
#include "ui/PlayBar.h"
#include "ui/PlaylistPage.h"
#include "ui/Sidebar.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"
#include "ui/WindowFrame.h"
#include "util/AppLog.h"

namespace {

constexpr const char* kReferer = "https://www.bilibili.com/";
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/126.0.0.0 Safari/537.36";

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size, nullptr,
                        nullptr);
    return out;
}

std::wstring chooseFolderW() {
    BROWSEINFOW browse{};
    browse.lpszTitle = L"选择本地音乐文件夹";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    LPITEMIDLIST item = SHBrowseForFolderW(&browse);
    if (item == nullptr) return {};
    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    if (!ok) return {};
    return path;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool isAudioFileName(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    static const std::set<std::string> kAudioExtensions = {
        ".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus", ".wma", ".ape"};
    return kAudioExtensions.count(extension) != 0;
}

std::vector<std::string> scanAudioFiles(const std::wstring& root) {
    std::vector<std::string> files;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        std::filesystem::path(root), std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!it->is_regular_file(error)) continue;
        if (isAudioFileName(it->path())) files.push_back(pathToUtf8(it->path()));
    }
    return files;
}

std::string sanitizeFileName(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    if (out.size() > 80) out.resize(80);
    return out;
}

bool writeBinaryFile(const std::wstring& path, const std::string& bytes) {
    std::FILE* file = _wfopen(path.c_str(), L"wb");
    if (file == nullptr) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return written == bytes.size();
}

std::wstring chooseImageFileW() {
    wchar_t fileBuffer[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"图片 (*.png;*.jpg;*.jpeg;*.bmp;*.webp)\0*.png;*.jpg;*.jpeg;*.bmp;*.webp\0所有文件\0*.*\0";
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = (DWORD)(sizeof(fileBuffer) / sizeof(wchar_t));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"选择歌单封面图片";
    if (GetOpenFileNameW(&dialog) == FALSE) return {};
    return fileBuffer;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size);
    return out;
}

std::string qualityLabel(int qualityId) {
    switch (qualityId) {
        case 30216: return "64K";
        case 30232: return "132K";
        case 30280: return "192K";
        case 30250: return "杜比全景声";
        case 30251: return "Hi-Res 无损";
        default: return qualityId > 0 ? ("音质 " + std::to_string(qualityId)) : "在线";
    }
}

std::string qualityKeyForId(int qualityId) {
    switch (qualityId) {
        case 30280: return "Q192";
        case 30232: return "Q132";
        case 30216: return "Q64";
        case 30251: return "HIRES";
        case 30250: return "DOLBY";
        default: return "AUTO";
    }
}

int qualityIdForKey(const std::string& key) {
    if (key == "Q192") return 30280;
    if (key == "Q132") return 30232;
    if (key == "Q64") return 30216;
    if (key == "HIRES") return 30251;
    if (key == "DOLBY") return 30250;
    return 0;
}

std::string qualityNameForId(int qualityId) {
    switch (qualityId) {
        case 30280: return "192K";
        case 30232: return "132K";
        case 30216: return "64K";
        case 30251: return "Hi-Res";
        case 30250: return "杜比";
        default: return "自动";
    }
}

void drawQrCode(const util::QrMatrix& matrix, ImVec2 topLeft, float targetSize) {
    if (!matrix.valid()) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int modules = matrix.size;
    float moduleSize = std::floor(targetSize / (float)modules);
    if (moduleSize < 1.0f) moduleSize = 1.0f;
    const float pixelSize = moduleSize * (float)modules;
    const float padding = 10.0f;
    draw->AddRectFilled(ImVec2(topLeft.x - padding, topLeft.y - padding),
                        ImVec2(topLeft.x + pixelSize + padding, topLeft.y + pixelSize + padding),
                        IM_COL32(255, 255, 255, 255), 8.0f);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (matrix.modules[(size_t)y * (size_t)modules + (size_t)x] == 0) continue;
            draw->AddRectFilled(
                ImVec2(topLeft.x + x * moduleSize, topLeft.y + y * moduleSize),
                ImVec2(topLeft.x + (x + 1) * moduleSize, topLeft.y + (y + 1) * moduleSize),
                IM_COL32(16, 16, 18, 255));
        }
    }
}

// 从「纯数字 / 空间主页链接 / 合集链接」里解析 id
long long parseIdAfter(const std::string& input, const char* marker) {
    const std::string key(marker);
    const size_t pos = input.find(key);
    if (pos == std::string::npos) return 0;
    size_t begin = pos + key.size();
    size_t end = begin;
    while (end < input.size() && input[end] >= '0' && input[end] <= '9') ++end;
    if (end == begin) return 0;
    try {
        return std::stoll(input.substr(begin, end - begin));
    } catch (const std::exception&) {
        return 0;
    }
}

bool isAllDigitsString(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

long long parseSpaceMidInput(const std::string& rawInput) {
    const std::string input = rawInput;
    if (isAllDigitsString(input)) {
        try {
            return std::stoll(input);
        } catch (const std::exception&) {
            return 0;
        }
    }
    long long mid = parseIdAfter(input, "space.bilibili.com/");
    if (mid > 0) return mid;
    return parseIdAfter(input, "uid=");
}

long long parseSeasonIdInput(const std::string& rawInput) {
    const std::string input = rawInput;
    if (isAllDigitsString(input)) {
        try {
            return std::stoll(input);
        } catch (const std::exception&) {
            return 0;
        }
    }
    long long id = parseIdAfter(input, "season_id=");
    if (id > 0) return id;
    id = parseIdAfter(input, "/lists/");
    if (id > 0) return id;
    return parseIdAfter(input, "seasonId=");
}

std::string formatModeName(int mode) {
    switch (mode) {
        case 0: return "顺序播放";
        case 2: return "单曲循环";
        case 3: return "随机播放";
        default: return "列表循环";
    }
}

}  // namespace

void App::init(GLFWwindow* window) {
    window_ = window;
    Ui::InitStyle();
    Ui::InitFonts();
    bili_.init();
    qualityId_ = qualityIdForKey(bili_.getSetting("audio_quality"));

    std::string engineError;
    if (!engine_.init(&engineError)) {
        setStatus("VLC 内核加载失败：" + engineError, true);
    }

    std::string loadError;
    if (!store_.load(&loadError)) {
        setStatus(loadError, true);
    } else if (!store_.playlists().empty()) {
        selectPlaylist(store_.playlists().front().id);
    }

    if (bili_.hasCookie()) {
        setStatus("正在验证 B 站登录态…");
        startLoginCheck();
    } else if (engine_.ready()) {
        setStatus("VLC " + engine_.version() + " · 未登录（登录后可解锁更高音质）");
    }
}

void App::shutdown() { engine_.shutdown(); }

void App::setStatus(const std::string& text, bool isError) {
    status_ = text;
    statusError_ = isError;
}

const StoredPlaylist* App::currentPlaylist() const {
    for (const StoredPlaylist& playlist : store_.playlists()) {
        if (playlist.id == selectedPlaylistId_) return &playlist;
    }
    return nullptr;
}

std::vector<const StoredSong*> App::currentSongs() const {
    return store_.songsOf(selectedPlaylistId_);
}

const StoredSong* App::findSong(long long songId) const {
    for (const StoredSong& song : store_.songs()) {
        if (song.id == songId) return &song;
    }
    return nullptr;
}

void App::selectPlaylist(long long playlistId) {
    selectedPlaylistId_ = playlistId;
    playingSongId_ = -1;
}

void App::postAsync(AsyncEvent event) {
    std::lock_guard<std::mutex> lock(asyncMutex_);
    asyncEvents_.push_back(std::move(event));
}

void App::startLoginCheck() {
    std::thread([this]() {
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::Login;
        const bili::NavInfo nav = bili_.fetchNav();
        event.ok = nav.ok;
        event.loggedIn = nav.loggedIn;
        event.userName = nav.userName;
        event.userId = nav.userId;
        event.message = nav.message;
        postAsync(std::move(event));
    }).detach();
}

void App::startOnlinePlayFor(long long songId, const std::string& bvid, long long cid,
                            const std::string& title, const std::string& artist) {
    playingSongId_ = songId;
    playingTitle_ = title;
    playingArtist_ = artist;
    pendingSongId_ = songId;
    currentIsOnline_ = true;
    curBvid_ = bvid;
    curCid_ = cid;
    backupIndex_ = 0;
    stallRetries_ = 0;
    const int qualityIdSnapshot = qualityId_;

    // 命中已解析地址 → 立刻播放（暂停/切歌回来后无需再等 1s 取流）
    auto cached = streamCache_.find(songId);
    if (cached != streamCache_.end() && cached->second.requestedQualityId == qualityIdSnapshot) {
        const int64_t nowSeconds = (int64_t)std::time(nullptr);
        if (cached->second.deadlineEpochSeconds == 0 ||
            cached->second.deadlineEpochSeconds - nowSeconds > 120) {
            std::string error;
            if (engine_.playUrl(cached->second.url, kReferer, kUserAgent, &error)) {
                engine_.setVolume(volume_);
                curStreamUrl_ = cached->second.url;
                curBackups_ = cached->second.backupUrls;
                curDeadlineEpoch_ = cached->second.deadlineEpochSeconds;
                lastStreamQualityId_ = cached->second.qualityId;
                pendingSongId_ = -1;
                stallWatchStartTick_ = 0;
                setStatus("正在播放：" + title + "（" + qualityNameForId(cached->second.qualityId) +
                          "）");
                AppLog::info("App", "命中流缓存直接播放 bvid=" + bvid);
                prefetchNextSong();
                return;
            }
        }
    }

    stallWatchStartTick_ = GetTickCount64();
    setStatus("正在取流：" + title + "（" + bvid + "，音质 " + qualityNameForId(qualityId_) + "）");
    std::thread([this, songId, bvid, cid, title, artist, qualityIdSnapshot]() {
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::Play;
        event.songId = songId;
        event.bvid = bvid;
        event.cid = cid;
        event.title = title;
        event.artist = artist;
        const bili::AudioStreamInfo stream = bili_.resolveAudio(bvid, cid, qualityIdSnapshot);
        event.ok = stream.ok;
        event.url = stream.url;
        event.qualityId = stream.qualityId;
        event.requestedQualityId = qualityIdSnapshot;
        event.downgraded = stream.downgraded;
        event.backupUrls = stream.backupUrls;
        event.deadlineEpochSeconds = stream.deadlineEpochSeconds;
        event.message = stream.message;
        postAsync(std::move(event));
    }).detach();
}

void App::startOnlinePlay(const StoredSong& song) {
    stallRetries_ = 0;
    if (song.bvid.empty() || song.cid <= 0) {
        setStatus("「" + song.title + "」缺少 bvid/cid，无法在线取流", true);
        return;
    }
    startOnlinePlayFor(song.id, song.bvid, song.cid, song.title, song.artist);
}

void App::startPastePlay(const std::string& text) {
    const std::string bvid = bili::BiliService::parseBvid(text);
    if (bvid.empty()) {
        setStatus("没有识别到 BV 号，请粘贴完整链接或 BV 号", true);
        return;
    }
    setStatus("正在解析：" + bvid + " …");
    std::thread([this, bvid]() {
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::BvPlay;
        event.bvid = bvid;
        const bili::VideoInfo video = bili_.fetchVideo(bvid);
        if (!video.ok) {
            event.ok = false;
            event.message = video.message;
            postAsync(std::move(event));
            return;
        }
        event.title = video.title;
        event.artist = video.artist;
        event.cid = video.cid;
        const bili::AudioStreamInfo stream = bili_.resolveAudio(bvid, video.cid, 0);
        event.ok = stream.ok;
        event.url = stream.url;
        event.qualityId = stream.qualityId;
        event.downgraded = stream.downgraded;
        event.message = stream.message;
        postAsync(std::move(event));
    }).detach();
}

void App::playSongById(long long songId) {
    const StoredSong* song = findSong(songId);
    if (song == nullptr) return;
    playingSongId_ = song->id;
    playingTitle_ = song->title;
    playingArtist_ = song->artist;
    if (!song->localPath.empty()) {
        currentIsOnline_ = false;
        stallWatchStartTick_ = 0;
        std::string error;
        if (engine_.playPath(utf8ToWide(song->localPath), &error)) {
            engine_.setVolume(volume_);
            setStatus("正在播放（本地）：" + song->title);
        } else {
            setStatus("播放失败：" + error, true);
        }
        return;
    }
    startOnlinePlay(*song);
}

void App::playFirstPlayable() {
    const std::vector<const StoredSong*> songs = currentSongs();
    for (const StoredSong* song : songs) {
        if (!song->localPath.empty()) {
            playSongById(song->id);
            return;
        }
    }
    for (const StoredSong* song : songs) {
        if (!song->bvid.empty()) {
            playSongById(song->id);
            return;
        }
    }
    setStatus("这个歌单没有可播放的曲目（没有本地文件，也没有 bvid）", true);
}

void App::stepSong(int direction) {
    const std::vector<const StoredSong*> songs = currentSongs();
    if (songs.empty()) {
        playFirstPlayable();
        return;
    }
    int currentIndex = -1;
    for (size_t i = 0; i < songs.size(); ++i) {
        if (songs[i]->id == playingSongId_) {
            currentIndex = (int)i;
            break;
        }
    }
    const int count = (int)songs.size();
    for (int step = 1; step <= count; ++step) {
        int index = 0;
        if (mode_ == 3) {
            index = (int)(std::rand() % count);
        } else if (mode_ == 2 && direction == 1) {
            index = currentIndex >= 0 ? currentIndex : 0;
        } else if (currentIndex >= 0) {
            index = (currentIndex + direction * step) % count;
            if (index < 0) index += count;
        } else {
            index = direction > 0 ? step - 1 : count - step;
        }
        if (index < 0 || index >= count) continue;
        const StoredSong* candidate = songs[(size_t)index];
        if (!candidate->localPath.empty() || !candidate->bvid.empty()) {
            playSongById(candidate->id);
            return;
        }
        if (mode_ == 2) break;
    }
    setStatus("附近没有可播放的曲目", true);
}

void App::syncPlaybackState() {
    hasMedia_ = engine_.ready() && engine_.lengthMs() > 0;
    positionMs_ = engine_.ready() ? (double)engine_.positionMs() : 0.0;
    durationMs_ = engine_.ready() ? (double)engine_.lengthMs() : 0.0;
    playing_ = engine_.ready() && engine_.isPlaying();
    engine_.pumpVolume();
    volume_ = engine_.volume();
    checkPlaybackStall();
}

void App::togglePlayback() {
    AppLog::info("App", "切换播放/暂停（托盘或全局媒体键）");
    if (!hasMedia_) {
        playFirstPlayable();
        return;
    }
    const bool wasPlaying = engine_.isPlaying();
    engine_.togglePause();
    if (wasPlaying) {
        // 暂停：停掉卡顿看门狗，避免暂停期间误判重取流
        stallWatchStartTick_ = 0;
    } else if (currentIsOnline_ && playingSongId_ >= 0) {
        // 恢复：重新开始观察，万一恢复失败还能自动重取
        stallWatchStartTick_ = GetTickCount64();
        stallRetries_ = 0;
    }
}

void App::playNext() {
    AppLog::info("App", "下一首（托盘或全局媒体键）");
    stepSong(1);
}

void App::playPrevious() {
    AppLog::info("App", "上一首（托盘或全局媒体键）");
    stepSong(-1);
}

void App::checkPlaybackStall() {
    if (!currentIsOnline_) return;
    // 暂停/缓冲期间位置本来就不前进，不能当成「卡住」去重取流
    if (!playing_) return;
    if (stallWatchStartTick_ == 0) return;
    if (positionMs_ > 800.0) {  // 已经开始正常出声，停止观察
        stallWatchStartTick_ = 0;
        return;
    }
    if (GetTickCount64() - stallWatchStartTick_ < 6000) return;
    stallWatchStartTick_ = 0;
    if (stallRetries_ >= 2) {
        setStatus("在线播放失败：主地址和备用地址都没能出声", true);
        return;
    }
    ++stallRetries_;
    if (backupIndex_ + 1 < (int)curBackups_.size()) {
        ++backupIndex_;
        std::string error;
        if (engine_.playUrl(curBackups_[(size_t)backupIndex_], kReferer, kUserAgent, &error)) {
            setStatus("主地址不可用，已切换备用地址 " + std::to_string(backupIndex_) + "/" +
                      std::to_string(curBackups_.size()));
            AppLog::warn("App", "切到备用地址播放：" + error);
            stallWatchStartTick_ = GetTickCount64();
            return;
        }
    }
    setStatus("取流地址可能已过期，正在重新取流…");
    AppLog::warn("App", "播放未出声，重新取流 bvid=" + curBvid_);
    startOnlinePlayFor(playingSongId_, curBvid_, curCid_, playingTitle_, playingArtist_);
}

void App::cancelQrLogin() {
    if (qrCancel_) qrCancel_->store(true);
    qrCancel_.reset();
    qrState_ = QrState::Idle;
    qrMatrix_ = {};
    qrMessage_.clear();
}

void App::startQrLogin() {
    cancelQrLogin();
    qrState_ = QrState::Loading;
    qrMessage_ = "正在获取二维码…";
    auto token = std::make_shared<std::atomic<bool>>(false);
    qrCancel_ = token;
    std::thread([this, token]() {
        const bili::QrCodeInfo info = bili_.qrGenerate();
        if (token->load()) return;
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::QrGenerate;
        event.ok = info.ok;
        event.qrUrl = info.url;
        event.qrKey = info.key;
        event.message = info.message;
        postAsync(std::move(event));
    }).detach();
}

void App::startQrPolling(const std::string& qrKey) {
    auto token = qrCancel_;
    if (!token) {
        token = std::make_shared<std::atomic<bool>>(false);
        qrCancel_ = token;
    }
    std::thread([this, qrKey, token]() {
        for (int round = 0; round < 90; ++round) {  // 最长 3 分钟
            for (int slice = 0; slice < 20; ++slice) {
                if (token->load()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (token->load()) return;
            const bili::QrPollInfo info = bili_.qrPoll(qrKey);
            if (token->load()) return;
            AsyncEvent event;
            event.kind = AsyncEvent::Kind::QrPoll;
            event.ok = info.ok;
            event.qrStatus = info.success ? 3 : (info.expired ? 2 : (info.scanned ? 1 : 0));
            event.qrCookies = info.cookies;
            event.message = info.message;
            postAsync(std::move(event));
            if (info.success || info.expired) return;
        }
    }).detach();
}

void App::applyQualitySelection(int qualityId) {
    qualityId_ = qualityId;
    bili_.setSetting("audio_quality", qualityKeyForId(qualityId));
    setStatus("音质档位：" + qualityNameForId(qualityId));
    if (currentIsOnline_ && !curBvid_.empty() && curCid_ > 0) {
        pendingSeekMs_ = positionMs_;
        startOnlinePlayFor(playingSongId_, curBvid_, curCid_, playingTitle_, playingArtist_);
    }
}

void App::pollAsync() {
    std::vector<AsyncEvent> events;
    {
        std::lock_guard<std::mutex> lock(asyncMutex_);
        events.swap(asyncEvents_);
    }
    for (const AsyncEvent& event : events) {
        if (event.kind == AsyncEvent::Kind::ImportProgress) {
            importProgressText_ = event.progressText;
            setStatus(event.progressText);
            continue;
        }
        if (event.kind == AsyncEvent::Kind::Prefetched) {
            if (event.ok && event.songId >= 0) {
                CachedStream entry;
                entry.url = event.url;
                entry.requestedQualityId = event.requestedQualityId;
                entry.qualityId = event.qualityId;
                entry.backupUrls = event.backupUrls;
                entry.deadlineEpochSeconds = event.deadlineEpochSeconds;
                streamCache_[event.songId] = std::move(entry);
                AppLog::info("App", "已预取下一首流地址 songId=" + std::to_string(event.songId));
            }
            continue;
        }
        if (event.kind == AsyncEvent::Kind::DownloadDone) {
            finishDownload(event);
            continue;
        }
        if (event.kind == AsyncEvent::Kind::LocalScanDone) {
            finishLocalScan(event);
            continue;
        }
        if (event.kind == AsyncEvent::Kind::ImportDone) {
            finishImport(event);
            continue;
        }
        if (event.kind == AsyncEvent::Kind::FavFolders) {
            favFoldersLoading_ = false;
            favFolders_ = event.favFolders;
            favFolderDialogRequested_ = true;
            if (!event.ok && !event.message.empty()) setStatus(event.message, true);
            continue;
        }
        if (event.kind == AsyncEvent::Kind::Login) {
            accountLoggedIn_ = event.loggedIn;
            accountName_ = event.userName;
            accountId_ = event.userId;
            if (event.loggedIn) {
                setStatus("已登录：" + event.userName + "（UID " + std::to_string(event.userId) + "）");
            } else {
                setStatus(event.ok ? "Cookie 无效或已过期，请重新登录" : "登录态校验失败：" + event.message,
                          true);
            }
            continue;
        }
        if (event.kind == AsyncEvent::Kind::QrGenerate) {
            if (!qrCancel_) continue;  // 已被取消
            if (!event.ok) {
                qrState_ = QrState::Idle;
                qrMessage_ = "获取二维码失败：" + event.message;
                setStatus(qrMessage_, true);
                continue;
            }
            qrKey_ = event.qrKey;
            if (!util::makeQrMatrix(event.qrUrl, &qrMatrix_)) {
                qrState_ = QrState::Idle;
                qrMessage_ = "二维码生成失败";
                continue;
            }
            qrState_ = QrState::Waiting;
            qrMessage_ = "等待扫码…";
            startQrPolling(event.qrKey);
            continue;
        }
        if (event.kind == AsyncEvent::Kind::QrPoll) {
            if (!qrCancel_) continue;
            switch (event.qrStatus) {
                case 1:
                    qrState_ = QrState::Scanned;
                    qrMessage_ = "已扫码，请在手机上确认";
                    break;
                case 2:
                    qrState_ = QrState::Expired;
                    qrMessage_ = "二维码已过期";
                    break;
                case 3:
                    bili_.importCookies(event.qrCookies);
                    accountLoggedIn_ = true;
                    qrState_ = QrState::Success;
                    qrMessage_ = "登录成功";
                    qrCloseRequested_ = true;
                    setStatus("扫码登录成功，正在同步账号信息…");
                    startLoginCheck();
                    break;
                default:
                    if (!event.ok && !event.message.empty()) qrMessage_ = event.message;
                    break;
            }
            continue;
        }
        if (event.kind == AsyncEvent::Kind::Play && event.songId != pendingSongId_) continue;
        if (!event.ok) {
            if (event.kind == AsyncEvent::Kind::Play) pendingSongId_ = -1;
            setStatus("取流失败：" + event.message, true);
            continue;
        }
        curStreamUrl_ = event.url;
        if (event.songId >= 0) {
            CachedStream entry;
            entry.url = event.url;
            entry.requestedQualityId = event.requestedQualityId;
            entry.qualityId = event.qualityId;
            entry.backupUrls = event.backupUrls;
            entry.deadlineEpochSeconds = event.deadlineEpochSeconds;
            streamCache_[event.songId] = std::move(entry);
        }
        std::string error;
        if (!engine_.playUrl(event.url, kReferer, kUserAgent, &error)) {
            setStatus("播放失败：" + error, true);
            continue;
        }
        engine_.setVolume(volume_);
        if (event.kind == AsyncEvent::Kind::BvPlay) playingSongId_ = -1;
        if (event.kind == AsyncEvent::Kind::Play) pendingSongId_ = -1;
        playingTitle_ = event.title;
        playingArtist_ = event.artist;
        curBvid_ = event.bvid;
        curBackups_ = event.backupUrls;
        curDeadlineEpoch_ = event.deadlineEpochSeconds;
        lastStreamQualityId_ = event.qualityId;
        backupIndex_ = 0;
        stallWatchStartTick_ = GetTickCount64();
        if (pendingSeekMs_ >= 0.0) {
            engine_.seekMs((int64_t)pendingSeekMs_);
            pendingSeekMs_ = -1.0;
        }
        std::string message = "在线播放：" + event.title + " · " + qualityLabel(event.qualityId);
        if (event.downgraded) message += "（请求的档位不可用，已自动降级）";
        setStatus(message);
        AppLog::info("App", message + " bvid=" + event.bvid);
        if (event.kind == AsyncEvent::Kind::Play) prefetchNextSong();
    }
}


// ================= M3：歌单管理与导入 =================

void App::openCreatePlaylistDialog() {
    nameDialogIsRename_ = false;
    nameDialogPlaylistId_ = -1;
    nameInput_[0] = '\0';
    nameDialogError_.clear();
    nameDialogRequested_ = true;
}

void App::openRenamePlaylistDialog() {
    const StoredPlaylist* playlist = currentPlaylist();
    if (playlist == nullptr) return;
    nameDialogIsRename_ = true;
    nameDialogPlaylistId_ = playlist->id;
    std::snprintf(nameInput_, sizeof(nameInput_), "%s", playlist->title.c_str());
    nameDialogError_.clear();
    nameDialogRequested_ = true;
}

void App::openDeletePlaylistDialog() {
    if (currentPlaylist() == nullptr) return;
    deletePlaylistId_ = selectedPlaylistId_;
    deleteDialogRequested_ = true;
}

void App::handleNameDialogConfirm() {
    const std::string normalized = LibraryStore::normalizeTitle(nameInput_);
    if (normalized.empty()) {
        nameDialogError_ = "歌单名不能为空";
        return;
    }
    nameDialogError_.clear();
    if (nameDialogIsRename_) {
        const int64_t id = nameDialogPlaylistId_;
        const StoredPlaylist* playlist = nullptr;
        for (const StoredPlaylist& item : store_.playlists()) {
            if (item.id == id) playlist = &item;
        }
        if (playlist == nullptr) {
            nameDialogRequested_ = false;
            return;
        }
        if (LibraryStore::normalizeTitle(playlist->title) == normalized) {
            nameDialogRequested_ = false;
            return;
        }
        PlaylistNameConflict conflict;
        if (store_.findConflict(normalized, id, &conflict)) {
            conflict_ = conflict;
            conflictAction_ = ConflictAction::Rename;
            conflictPlaylistId_ = id;
            nameDialogRequested_ = false;
            conflictDialogRequested_ = true;
            return;
        }
        if (store_.renamePlaylist(id, normalized)) {
            setStatus("已重命名歌单：" + normalized);
        } else {
            setStatus("重命名失败", true);
        }
        nameDialogRequested_ = false;
        return;
    }

    PlaylistNameConflict conflict;
    if (store_.findConflict(normalized, -1, &conflict)) {
        conflict_ = conflict;
        conflictAction_ = ConflictAction::Create;
        nameDialogRequested_ = false;
        conflictDialogRequested_ = true;
        return;
    }
    const int64_t id = store_.createPlaylist(normalized);
    selectPlaylist(id);
    setStatus("已新建歌单：" + normalized);
    nameDialogRequested_ = false;
}

void App::handleConflictChoice(int choice) {
    conflictDialogRequested_ = false;
    const ConflictAction action = conflictAction_;
    conflictAction_ = ConflictAction::None;
    if (choice == 2) {
        pendingImportValid_ = false;
        setStatus("已取消");
        return;
    }
    const std::string desired =
        choice == 0 ? conflict_.desiredTitle : conflict_.suggestedTitle;

    if (action == ConflictAction::Create) {
        const int64_t id = choice == 0 ? store_.createPlaylistOverwriting(desired)
                                       : store_.createPlaylist(desired);
        selectPlaylist(id);
        setStatus("已新建歌单：" + desired);
        return;
    }
    if (action == ConflictAction::Rename) {
        const int64_t id = conflictPlaylistId_;
        if (choice == 0) store_.deletePlaylistByTitle(desired, id);
        if (store_.renamePlaylist(id, desired)) {
            setStatus("已重命名歌单：" + desired);
        } else {
            setStatus("重命名失败", true);
        }
        return;
    }

    if (action == ConflictAction::ImportPending) {
        if (!pendingImportedValid_) return;
        std::vector<StoredSong> songs;
        songs.reserve(pendingImportedResult_.songs.size());
        for (const bili::ImportedSong& imported : pendingImportedResult_.songs) {
            StoredSong song;
            song.bvid = imported.bvid;
            song.cid = imported.cid;
            song.title = imported.title;
            song.artist = imported.artist;
            song.coverUrl = imported.coverUrl;
            song.durationMs = imported.durationMs;
            songs.push_back(std::move(song));
        }
        const int64_t id =
            choice == 0
                ? store_.createPlaylistOverwriting(desired, pendingImportedSourceType_,
                                                   pendingImportedTitle_)
                : store_.createPlaylist(desired, pendingImportedSourceType_, pendingImportedTitle_);
        const int added = store_.addSongs(id, songs);
        selectPlaylist(id);
        setStatus("「" + desired + "」导入完成：新增 " + std::to_string(added) + " 首");
        pendingImportedValid_ = false;
        return;
    }

    // 导入场景
    if (!pendingImportValid_) return;
    ImportContext context = pendingImport_;
    context.overwrite = choice == 0;
    if (choice == 1) context.title = conflict_.suggestedTitle;
    startImportWorker(context);
}

void App::beginImport(const ImportContext& context) {
    // 先取数据、拿到真实标题后再判重（UP主/合集要等接口返回昵称/合集名）
    startImportWorker(context);
}

void App::startImportWorker(const ImportContext& context) {
    importRunning_ = true;
    pendingImportValid_ = false;
    importProgressText_ = "正在导入「" + context.title + "」…";
    setStatus(importProgressText_);
    AppLog::info("App", "开始导入：" + context.title + " source=" + context.sourceType);

    const bili::ImportProgressFn progress = [this](const std::string& text, int current, int total) {
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::ImportProgress;
        event.ok = true;
        event.progressText = text;
        event.progressCurrent = current;
        event.progressTotal = total;
        postAsync(std::move(event));
    };

    std::thread([this, context, progress]() {
        bili::ImportedSongsResult result;
        if (context.toView) {
            result = bili_.importToView(progress);
        } else if (context.seasonId > 0) {
            result = bili_.importSeason(context.seasonId, progress);
        } else if (context.spaceMid > 0) {
            result = bili_.importSpaceUploads(context.spaceMid, progress);
        } else {
            result = bili_.importFavFolder(context.folderId, progress);
        }
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::ImportDone;
        event.ok = result.ok;
        event.imported = std::move(result);
        event.message = event.imported.message;
        event.desiredTitle = context.title;
        event.overwrite = context.overwrite;
        event.sourceType = context.sourceType;
        event.sourceTitle = context.sourceTitle;
        postAsync(std::move(event));
    }).detach();
}

void App::loadFavFolders() {
    if (!accountLoggedIn_) {
        setStatus("请先登录再导入收藏夹", true);
        importSourceDialogRequested_ = false;
        return;
    }
    favFoldersLoading_ = true;
    favFolders_.clear();
    favFolderDialogRequested_ = true;
    const long long mid = accountId_;
    std::thread([this, mid]() {
        std::string error;
        std::vector<bili::FavFolder> folders = bili_.favFolders(mid, &error);
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::FavFolders;
        event.ok = error.empty();
        event.message = error;
        event.favFolders = std::move(folders);
        postAsync(std::move(event));
    }).detach();
}

void App::finishImport(const AsyncEvent& event) {
    importRunning_ = false;
    if (!event.ok) {
        setStatus("导入失败：" + event.message, true);
        importProgressText_.clear();
        return;
    }
    std::string title = event.desiredTitle;
    if (title.empty()) title = event.imported.suggestedTitle;
    if (title.empty()) title = "导入歌单";
    const std::string sourceTitle = event.sourceTitle.empty() ? title : event.sourceTitle;

    std::vector<StoredSong> songs;
    songs.reserve(event.imported.songs.size());
    for (const bili::ImportedSong& imported : event.imported.songs) {
        StoredSong song;
        song.bvid = imported.bvid;
        song.cid = imported.cid;
        song.title = imported.title;
        song.artist = imported.artist;
        song.coverUrl = imported.coverUrl;
        song.durationMs = imported.durationMs;
        songs.push_back(std::move(song));
    }

    // 同一来源已经导入过 → 直接复用那个歌单
    if (const StoredPlaylist* existing = store_.findBySource(event.sourceType, sourceTitle)) {
        const int added = store_.addSongs(existing->id, songs);
        selectPlaylist(existing->id);
        setStatus("「" + existing->title + "」导入完成：新增 " + std::to_string(added) + " 首");
        importProgressText_.clear();
        return;
    }
    // 重名 → 暂存数据，弹「覆盖 / 自动重命名」
    PlaylistNameConflict conflict;
    if (!event.overwrite && store_.findConflict(title, -1, &conflict)) {
        pendingImportedResult_ = event.imported;
        pendingImportedSourceType_ = event.sourceType;
        pendingImportedTitle_ = title;
        pendingImportedValid_ = true;
        conflict_ = conflict;
        conflictAction_ = ConflictAction::ImportPending;
        conflictDialogRequested_ = true;
        importProgressText_.clear();
        return;
    }
    const int64_t playlistId =
        event.overwrite ? store_.createPlaylistOverwriting(title, event.sourceType, sourceTitle)
                        : store_.createPlaylist(title, event.sourceType, sourceTitle);
    const int added = store_.addSongs(playlistId, songs);
    selectPlaylist(playlistId);
    setStatus("「" + title + "」导入完成：新增 " + std::to_string(added) + " 首");
    importProgressText_.clear();
    AppLog::info("App", "导入完成 " + title + " 新增 " + std::to_string(added));
}

void App::applyMultiAction(long long targetPlaylistId) {
    if (selectedSongIds_.empty()) {
        setStatus("没有选中的曲目");
        return;
    }
    if (multiAction_ == 2 && targetPlaylistId == selectedPlaylistId_) {
        setStatus("移动目标不能是当前歌单", true);
        return;
    }
    const std::vector<int64_t> ids(selectedSongIds_.begin(), selectedSongIds_.end());
    std::string targetTitle;
    for (const StoredPlaylist& playlist : store_.playlists()) {
        if (playlist.id == targetPlaylistId) targetTitle = playlist.title;
    }
    const int count = multiAction_ == 1 ? store_.copySongsTo(targetPlaylistId, ids)
                                        : store_.moveSongsTo(targetPlaylistId, ids);
    if (multiAction_ == 2) selectedSongIds_.clear();
    setStatus((multiAction_ == 1 ? "已复制 " : "已移动 ") + std::to_string(count) + " 首到「" +
              targetTitle + "」");
}

void App::chooseLocalCover() {
    const StoredPlaylist* playlist = currentPlaylist();
    if (playlist == nullptr) return;
    const std::wstring source = chooseImageFileW();
    if (source.empty()) return;
    std::wstring extension = L".png";
    const size_t dot = source.find_last_of(L'.');
    if (dot != std::wstring::npos && source.size() - dot <= 6) extension = source.substr(dot);
    std::wstring coversDir = LibraryStore::dataDir() + L"\\covers";
    CreateDirectoryW(coversDir.c_str(), nullptr);
    const std::wstring destination =
        coversDir + L"\\playlist_" + std::to_wstring(playlist->id) + extension;
    if (CopyFileW(source.c_str(), destination.c_str(), FALSE) == FALSE) {
        setStatus("复制封面图片失败", true);
        return;
    }
    if (store_.setPlaylistCover(playlist->id, "", wideToUtf8(destination))) {
        setStatus("已设置歌单封面（本地图片）");
    } else {
        setStatus("设置封面失败", true);
    }
}


void App::prefetchNextSong() {
    const std::vector<const StoredSong*> songs = currentSongs();
    if (songs.size() <= 1) return;
    int index = -1;
    for (size_t i = 0; i < songs.size(); ++i) {
        if (songs[i]->id == playingSongId_) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) return;
    const int qualityIdSnapshot = qualityId_;
    for (int step = 1; step <= (int)songs.size(); ++step) {
        const int next = (index + step) % (int)songs.size();
        const StoredSong* song = songs[(size_t)next];
        if (!song->localPath.empty() || song->bvid.empty() || song->cid <= 0) continue;
        auto it = streamCache_.find(song->id);
        if (it != streamCache_.end() && it->second.requestedQualityId == qualityIdSnapshot) {
            const int64_t nowSeconds = (int64_t)std::time(nullptr);
            if (it->second.deadlineEpochSeconds == 0 ||
                it->second.deadlineEpochSeconds - nowSeconds > 120) {
                return;  // 已经预取过
            }
        }
        const long long songId = song->id;
        const std::string bvid = song->bvid;
        const long long cid = song->cid;
        const std::string title = song->title;
        const std::string artist = song->artist;
        std::thread([this, songId, bvid, cid, title, artist, qualityIdSnapshot]() {
            AsyncEvent event;
            event.kind = AsyncEvent::Kind::Prefetched;
            event.songId = songId;
            event.title = title;
            event.artist = artist;
            const bili::AudioStreamInfo stream = bili_.resolveAudio(bvid, cid, qualityIdSnapshot);
            event.ok = stream.ok;
            event.url = stream.url;
            event.qualityId = stream.qualityId;
            event.requestedQualityId = qualityIdSnapshot;
            event.backupUrls = stream.backupUrls;
            event.deadlineEpochSeconds = stream.deadlineEpochSeconds;
            event.message = stream.message;
            postAsync(std::move(event));
        }).detach();
        return;
    }
}

void App::startDownloadCurrent() {
    if (playingSongId_ < 0) {
        setStatus("当前没有播放曲目", true);
        return;
    }
    const StoredSong* song = findSong(playingSongId_);
    if (song == nullptr) {
        setStatus("找不到当前曲目", true);
        return;
    }
    if (curStreamUrl_.empty()) {
        setStatus(song->localPath.empty() ? "当前曲目没有可下载的在线地址" : "当前曲目已经是本地文件",
                  true);
        return;
    }
    const std::string title = song->title;
    const std::string bvid = song->bvid;
    const std::string url = curStreamUrl_;
    const int64_t songId = playingSongId_;
    setStatus("开始下载「" + title + "」…");
    std::thread([this, title, bvid, url, songId]() {
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::DownloadDone;
        event.songId = songId;
        net::HttpResponse response;
        std::string error;
        const std::vector<std::pair<std::string, std::string>> headers = {
            {"User-Agent", kUserAgent}, {"Referer", kReferer}};
        if (!net::httpGet(url, headers, &response, &error) || response.body.empty()) {
            event.ok = false;
            event.message = error.empty() ? "下载内容为空" : error;
            postAsync(std::move(event));
            return;
        }
        std::wstring downloadsDir = LibraryStore::dataDir() + L"\\downloads";
        CreateDirectoryW(downloadsDir.c_str(), nullptr);
        std::string fileName = sanitizeFileName(title);
        if (!bvid.empty()) fileName += " [" + bvid + "]";
        fileName += ".m4s";
        const std::wstring destination = downloadsDir + L"\\" + utf8ToWide(fileName);
        if (!writeBinaryFile(destination, response.body)) {
            event.ok = false;
            event.message = "写入文件失败";
        } else {
            event.ok = true;
            event.filePath = wideToUtf8(destination);
            event.message = "下载完成";
        }
        postAsync(std::move(event));
    }).detach();
}

void App::scanLocalMusic() {
    const std::wstring folder = chooseFolderW();
    if (folder.empty()) {
        setStatus("已取消扫描");
        return;
    }
    setStatus("正在扫描本地音乐…");
    std::thread([this, folder]() {
        std::vector<std::string> files = scanAudioFiles(folder);
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::LocalScanDone;
        event.ok = true;
        event.localPaths = std::move(files);
        postAsync(std::move(event));
    }).detach();
}

void App::finishLocalScan(const AsyncEvent& event) {
    if (!event.ok) {
        setStatus("扫描失败", true);
        return;
    }
    int64_t playlistId = -1;
    if (const StoredPlaylist* existing = store_.findBySource("LOCAL", "本地音乐")) {
        playlistId = existing->id;
    } else {
        for (const StoredPlaylist& playlist : store_.playlists()) {
            if (playlist.title == "本地音乐") {
                playlistId = playlist.id;
                break;
            }
        }
    }
    if (playlistId < 0) {
        playlistId = store_.createPlaylist("本地音乐", "LOCAL", "本地音乐");
    }
    std::vector<StoredSong> songs;
    songs.reserve(event.localPaths.size());
    for (const std::string& path : event.localPaths) {
        const std::filesystem::path filePath = pathFromUtf8(path);
        StoredSong song;
        song.title = pathToUtf8(filePath.stem());
        song.artist = pathToUtf8(filePath.parent_path().filename());
        song.localPath = path;
        songs.push_back(std::move(song));
    }
    const int added = store_.addSongs(playlistId, songs);
    selectPlaylist(playlistId);
    setStatus("扫描完成：发现 " + std::to_string(event.localPaths.size()) + " 个音频文件，新增 " +
              std::to_string(added) + " 首");
    AppLog::info("App", "本地音乐扫描：文件 " + std::to_string(event.localPaths.size()) +
                            "，新增 " + std::to_string(added));
}

void App::finishDownload(const AsyncEvent& event) {
    if (!event.ok) {
        setStatus("下载失败：" + event.message, true);
        return;
    }
    if (store_.setSongLocalPath(event.songId, event.filePath)) {
        setStatus("已下载到本地：" + event.filePath);
        AppLog::info("App", "下载完成：" + event.filePath);
    } else {
        setStatus("已下载，但更新曲目路径失败", true);
    }
}

void App::drawM3Dialogs() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 center(display.x * 0.5f, display.y * 0.5f);

    // ---- 新建 / 重命名歌单 ----
    if (nameDialogRequested_) ImGui::OpenPopup("歌单名称");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("歌单名称", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!nameDialogRequested_) ImGui::CloseCurrentPopup();
        ImGui::TextUnformatted(nameDialogIsRename_ ? "重命名歌单" : "新建歌单");
        ImGui::SetNextItemWidth(360.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##name", nameInput_, sizeof(nameInput_),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            handleNameDialogConfirm();
        }
        if (!nameDialogError_.empty()) {
            ImGui::TextColored(ImVec4(0.93f, 0.25f, 0.25f, 1.0f), "%s", nameDialogError_.c_str());
        }
        if (ImGui::Button("确定", ImVec2(110.0f, 32.0f))) handleNameDialogConfirm();
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            nameDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- 重名冲突 ----
    if (conflictDialogRequested_) ImGui::OpenPopup("歌单重名");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("歌单重名", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!conflictDialogRequested_) ImGui::CloseCurrentPopup();
        ImGui::TextWrapped("已存在同名歌单「%s」（%d 首）。", conflict_.existingTitle.c_str(),
                           conflict_.existingSongCount);
        ImGui::TextDisabled("覆盖会删除已有的同名歌单及其所有曲目。");
        ImGui::Spacing();
        if (ImGui::Button("覆盖", ImVec2(110.0f, 32.0f))) handleConflictChoice(0);
        ImGui::SameLine();
        const std::string renameLabel = "自动重命名「" + conflict_.suggestedTitle + "」";
        if (ImGui::Button(renameLabel.c_str(), ImVec2(0.0f, 32.0f))) handleConflictChoice(1);
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100.0f, 32.0f))) {
            handleConflictChoice(2);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- 删除确认 ----
    if (deleteDialogRequested_) ImGui::OpenPopup("删除歌单");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("删除歌单", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!deleteDialogRequested_) ImGui::CloseCurrentPopup();
        std::string title;
        int count = 0;
        for (const StoredPlaylist& playlist : store_.playlists()) {
            if (playlist.id == deletePlaylistId_) {
                title = playlist.title;
                count = store_.songCount(playlist.id);
            }
        }
        ImGui::TextWrapped("删除歌单「%s」及其中 %d 首曲目？此操作不可恢复。", title.c_str(), count);
        ImGui::Spacing();
        if (ImGui::Button("删除", ImVec2(110.0f, 32.0f))) {
            if (store_.deletePlaylist(deletePlaylistId_)) {
                if (selectedPlaylistId_ == deletePlaylistId_) {
                    selectedPlaylistId_ =
                        store_.playlists().empty() ? -1 : store_.playlists().front().id;
                }
                setStatus("已删除歌单");
            }
            deleteDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            deleteDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- 导入来源 ----
    if (importSourceDialogRequested_) ImGui::OpenPopup("导入歌单");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("导入歌单", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!importSourceDialogRequested_) ImGui::CloseCurrentPopup();
        ImGui::TextUnformatted("选择要导入的来源：");
        ImGui::Spacing();
        if (ImGui::Button("收藏夹", ImVec2(150.0f, 36.0f))) {
            importSourceDialogRequested_ = false;
            loadFavFolders();
        }
        ImGui::SameLine();
        if (ImGui::Button("稍后再看", ImVec2(150.0f, 36.0f))) {
            importSourceDialogRequested_ = false;
            ImportContext context;
            context.title = "稍后再看";
            context.sourceType = "TOVIEW";
            context.sourceTitle = "稍后再看";
            context.toView = true;
            beginImport(context);
        }
        ImGui::Spacing();
        if (ImGui::Button("UP主投稿", ImVec2(150.0f, 36.0f))) {
            importSourceDialogRequested_ = false;
            importInputKind_ = 1;
            importInput_[0] = '\0';
            importInputError_.clear();
            importInputRequested_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("合集", ImVec2(150.0f, 36.0f))) {
            importSourceDialogRequested_ = false;
            importInputKind_ = 2;
            importInput_[0] = '\0';
            importInputError_.clear();
            importInputRequested_ = true;
        }
        ImGui::Spacing();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            importSourceDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- UP主 UID / 合集 ID 输入 ----
    if (importInputRequested_) ImGui::OpenPopup("导入 UP主 / 合集");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("导入 UP主 / 合集", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!importInputRequested_) ImGui::CloseCurrentPopup();
        if (importInputKind_ == 1) {
            ImGui::TextUnformatted("输入 UP 主 UID 或空间主页链接：");
        } else {
            ImGui::TextUnformatted("输入合集 ID 或合集链接：");
        }
        ImGui::SetNextItemWidth(460.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool submittedByEnter =
            ImGui::InputText("##import_input", importInput_, sizeof(importInput_),
                             ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled(importInputKind_ == 1
                                ? "例如：668485527 或 https://space.bilibili.com/668485527"
                                : "例如：123456 或 .../lists/123456?type=season");
        if (!importInputError_.empty()) {
            ImGui::TextColored(ImVec4(0.93f, 0.25f, 0.25f, 1.0f), "%s", importInputError_.c_str());
        }
        ImGui::Spacing();
        bool confirm = submittedByEnter;
        if (ImGui::Button("确定", ImVec2(110.0f, 32.0f))) confirm = true;
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            importInputRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (confirm) {
            const std::string input = importInput_;
            ImportContext context;
            bool valid = false;
            if (importInputKind_ == 1) {
                const long long mid = parseSpaceMidInput(input);
                if (mid <= 0) {
                    importInputError_ = "没有识别到 UID，请检查输入";
                } else {
                    context.sourceType = "UPLOAD";
                    context.spaceMid = mid;
                    valid = true;
                }
            } else {
                const long long seasonId = parseSeasonIdInput(input);
                if (seasonId <= 0) {
                    importInputError_ = "没有识别到合集 ID，请检查输入";
                } else {
                    context.sourceType = "SEASON";
                    context.seasonId = seasonId;
                    valid = true;
                }
            }
            if (valid) {
                importInputRequested_ = false;
                ImGui::CloseCurrentPopup();
                startImportWorker(context);
            }
        }
        ImGui::EndPopup();
    }

    // ---- 多选：选择目标歌单 ----
    if (targetPlaylistDialogRequested_) ImGui::OpenPopup("选择目标歌单");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("选择目标歌单", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!targetPlaylistDialogRequested_) ImGui::CloseCurrentPopup();
        ImGui::TextUnformatted(multiAction_ == 1 ? "复制到哪个歌单：" : "移动到哪个歌单：");
        ImGui::TextDisabled("已选 %d 首", (int)selectedSongIds_.size());
        long long pendingTarget = -1;
        ImGui::BeginChild("##target_list", ImVec2(420.0f, 260.0f), ImGuiChildFlags_Borders);
        for (const StoredPlaylist& playlist : store_.playlists()) {
            const std::string label =
                Ui::DisplayText(playlist.title) + "  (" + std::to_string(store_.songCount(playlist.id)) +
                " 首)";
            if (ImGui::Selectable(label.c_str())) {
                pendingTarget = playlist.id;
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            targetPlaylistDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (pendingTarget >= 0) {
            targetPlaylistDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
            applyMultiAction(pendingTarget);
        }
        ImGui::EndPopup();
    }

    // ---- 多选：删除曲目确认 ----
    if (deleteSongsDialogRequested_) ImGui::OpenPopup("删除曲目");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("删除曲目", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!deleteSongsDialogRequested_) ImGui::CloseCurrentPopup();
        ImGui::TextWrapped("从当前歌单删除选中的 %d 首曲目？此操作不可恢复。",
                           (int)selectedSongIds_.size());
        ImGui::Spacing();
        if (ImGui::Button("删除", ImVec2(110.0f, 32.0f))) {
            const std::vector<int64_t> ids(selectedSongIds_.begin(), selectedSongIds_.end());
            const int removed = store_.removeSongs(ids);
            selectedSongIds_.clear();
            setStatus("已删除 " + std::to_string(removed) + " 首曲目");
            deleteSongsDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            deleteSongsDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- 封面：从歌单视频选 ----
    if (coverSongDialogRequested_) ImGui::OpenPopup("从歌单视频选封面");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("从歌单视频选封面", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!coverSongDialogRequested_) ImGui::CloseCurrentPopup();
        const StoredPlaylist* playlist = currentPlaylist();
        if (playlist == nullptr) {
            ImGui::TextDisabled("没有选中的歌单");
        } else {
            ImGui::TextUnformatted("选择一首歌，用它的视频封面作为歌单封面：");
            long long pendingSongId = -1;
            ImGui::BeginChild("##cover_song_list", ImVec2(460.0f, 300.0f), ImGuiChildFlags_Borders);
            for (const StoredSong* song : currentSongs()) {
                if (song->coverUrl.empty()) continue;
                const std::string label = Ui::DisplayText(song->title);
                if (ImGui::Selectable(label.c_str())) pendingSongId = song->id;
            }
            ImGui::EndChild();
            if (pendingSongId >= 0) {
                for (const StoredSong* song : currentSongs()) {
                    if (song->id != pendingSongId) continue;
                    if (store_.setPlaylistCover(playlist->id, song->coverUrl, "")) {
                        setStatus("已设置歌单封面（来自歌单视频）");
                    }
                    break;
                }
                coverSongDialogRequested_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            coverSongDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- 收藏夹选择 ----
    if (favFolderDialogRequested_) ImGui::OpenPopup("选择收藏夹");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("选择收藏夹", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!favFolderDialogRequested_) ImGui::CloseCurrentPopup();
        if (favFoldersLoading_) {
            ImGui::TextUnformatted("正在获取收藏夹列表…");
        } else if (favFolders_.empty()) {
            ImGui::TextDisabled("没有拿到收藏夹（确认已登录且账号里有收藏夹）");
        } else {
            ImGui::TextUnformatted("选择一个收藏夹导入：");
            ImGui::BeginChild("##fav_list", ImVec2(420.0f, 260.0f), ImGuiChildFlags_Borders);
            for (const bili::FavFolder& folder : favFolders_) {
                const std::string label =
                    Ui::DisplayText(folder.title) + "  (" + std::to_string(folder.mediaCount) + " 首)";
                if (ImGui::Selectable(label.c_str())) {
                    favFolderDialogRequested_ = false;
                    ImportContext context;
                    context.title = folder.title;
                    context.sourceType = "FAV";
                    context.sourceTitle = folder.title;
                    context.folderId = folder.id;
                    ImGui::CloseCurrentPopup();
                    beginImport(context);
                }
            }
            ImGui::EndChild();
        }
        ImGui::Spacing();
        if (importRunning_) ImGui::TextDisabled("%s", importProgressText_.c_str());
        if (ImGui::Button("刷新", ImVec2(110.0f, 32.0f))) {
            favFoldersLoading_ = true;
            favFolders_.clear();
            std::thread([this]() {
                std::string error;
                std::vector<bili::FavFolder> folders = bili_.favFolders(accountId_, &error);
                AsyncEvent event;
                event.kind = AsyncEvent::Kind::FavFolders;
                event.ok = error.empty();
                event.message = error;
                event.favFolders = std::move(folders);
                postAsync(std::move(event));
            }).detach();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
            favFolderDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::drawDialogs() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    ImGui::PushStyleColor(ImGuiCol_Text, Ui::Vec4(Ui::Colors::Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, Ui::Vec4(Ui::Colors::Text3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Ui::Vec4(Ui::Colors::Dialog));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Ui::Vec4(Ui::Colors::Dialog));
    ImGui::PushStyleColor(ImGuiCol_Border, Ui::Vec4(Ui::Colors::Divider));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Ui::Vec4(Ui::Colors::BgTitleBar));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Ui::Vec4(Ui::Colors::Hover));
    ImGui::PushStyleColor(ImGuiCol_Border, Ui::Vec4(Ui::Colors::Divider));
    ImGui::PushStyleColor(ImGuiCol_Button, Ui::Vec4(Ui::Colors::Brand));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Ui::Vec4(Ui::Colors::BrandHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Ui::Vec4(Ui::Colors::BrandHover));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (linkDialogRequested_) {
        ImGui::OpenPopup("播放链接");
        linkDialogRequested_ = false;
    }
    if (loginDialogRequested_) {
        ImGui::OpenPopup("登录 B 站");
        loginDialogRequested_ = false;
        loginTabQr_ = true;
        startQrLogin();
    }

    if (ImGui::BeginPopupModal("播放链接", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("粘贴 B 站视频链接或 BV 号：");
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##link", linkInput_, sizeof(linkInput_));
        ImGui::Spacing();
        if (ImGui::Button("播放", ImVec2(110.0f, 32.0f))) {
            const std::string input = linkInput_;
            if (!input.empty()) {
                startPastePlay(input);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(110.0f, 32.0f)) || escapePressed) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("登录 B 站", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (qrCloseRequested_) {
            qrCloseRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        // Tab 切换
        if (ImGui::Selectable("扫码登录", loginTabQr_, 0, ImVec2(110.0f, 30.0f))) {
            if (!loginTabQr_) {
                loginTabQr_ = true;
                startQrLogin();
            }
        }
        ImGui::SameLine();
        if (ImGui::Selectable("粘贴 Cookie", !loginTabQr_, 0, ImVec2(120.0f, 30.0f))) {
            if (loginTabQr_) {
                loginTabQr_ = false;
                cancelQrLogin();
            }
        }
        ImGui::Separator();

        if (loginTabQr_) {
            ImGui::TextUnformatted("用 B 站手机 App 扫下面的二维码：");
            const float qrTarget = 200.0f;
            const float box = qrTarget + 24.0f;
            const ImVec2 qrOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 qrTopLeft(qrOrigin.x + 12.0f, qrOrigin.y + 12.0f);
            ImGui::Dummy(ImVec2(box, box));
            if (qrMatrix_.valid()) {
                drawQrCode(qrMatrix_, qrTopLeft, qrTarget);
            } else {
                ImGui::SetCursorScreenPos(ImVec2(qrTopLeft.x + 20.0f, qrTopLeft.y + qrTarget * 0.5f - 10.0f));
                ImGui::TextDisabled("%s", qrMessage_.empty() ? "正在获取二维码…" : qrMessage_.c_str());
            }
            ImGui::Spacing();
            switch (qrState_) {
                case QrState::Loading:
                    ImGui::TextDisabled("正在获取二维码…");
                    break;
                case QrState::Waiting:
                    ImGui::TextDisabled("等待扫码…（二维码 3 分钟内有效）");
                    break;
                case QrState::Scanned:
                    ImGui::TextColored(ImVec4(0.93f, 0.25f, 0.25f, 1.0f), "已扫码，请在手机上确认登录");
                    break;
                case QrState::Expired:
                    ImGui::TextColored(ImVec4(0.93f, 0.25f, 0.25f, 1.0f), "二维码已过期，请刷新");
                    break;
                case QrState::Success:
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.45f, 1.0f), "登录成功");
                    break;
                default:
                    if (!qrMessage_.empty()) ImGui::TextDisabled("%s", qrMessage_.c_str());
                    break;
            }
            ImGui::Spacing();
            if (ImGui::Button("刷新二维码", ImVec2(120.0f, 32.0f))) startQrLogin();
            ImGui::SameLine();
            if (ImGui::Button("关闭", ImVec2(100.0f, 32.0f)) || escapePressed) ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextUnformatted("浏览器登录 bilibili.com 后，把整段 Cookie 粘贴到这里：");
            ImGui::TextDisabled("格式：SESSDATA=xxx; bili_jct=yyy; DedeUserID=123; ...");
            ImGui::InputTextMultiline("##cookie", cookieInput_, sizeof(cookieInput_),
                                      ImVec2(560.0f, 200.0f));
            ImGui::Spacing();
            if (ImGui::Button("保存并登录", ImVec2(130.0f, 32.0f))) {
                const int count = bili_.importCookieText(cookieInput_);
                if (count > 0) {
                    setStatus("Cookie 已保存，正在验证登录态…");
                    startLoginCheck();
                } else {
                    setStatus("Cookie 解析失败：没找到 key=value", true);
                }
                cookieInput_[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("清除登录", ImVec2(110.0f, 32.0f))) {
                bili_.clearCookie();
                accountLoggedIn_ = false;
                accountName_.clear();
                accountId_ = 0;
                setStatus("已退出登录");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("关闭", ImVec2(100.0f, 32.0f)) || escapePressed) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!ImGui::IsPopupOpen("登录 B 站") && qrState_ != QrState::Idle) {
        cancelQrLogin();
    }
    drawM3Dialogs();

    ImGui::PopStyleVar(8);
    ImGui::PopStyleColor(11);
}

void App::draw() {
    pollAsync();
    syncPlaybackState();

    ImGuiIO& io = ImGui::GetIO();
    const float titleBarHeight = Ui::Dimens::TitleBarHeight;
    const float playerHeight = Ui::Dimens::PlayerHeight;
    const float mainHeight = io.DisplaySize.y - titleBarHeight - playerHeight;
    const float sidebarWidth = Ui::Dimens::SidebarWidth;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##root", nullptr, rootFlags);

    const bool maximized = Ui::WindowIsMaximized(window_);
    ImGui::SetCursorPos(ImVec2(0, 0));
    Ui::DrawTitleBar(window_, maximized);

    // ---------------- 侧栏 ----------------
    ImGui::SetCursorPos(ImVec2(0, titleBarHeight));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Ui::Vec4(Ui::Colors::BgSide));
    ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, mainHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    Ui::SidebarActions sidebarActions;
    Ui::DrawSidebar(store_, selectedPlaylistId_, mainHeight, status_, statusError_,
                    (int)store_.playlists().size(), accountLoggedIn_, accountName_, accountId_,
                    &sidebarActions);
    ImGui::EndChild();

    // ---------------- 歌单页 ----------------
    ImGui::SetCursorPos(ImVec2(sidebarWidth, titleBarHeight));
    ImGui::BeginChild("##content", ImVec2(io.DisplaySize.x - sidebarWidth, mainHeight),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    Ui::PlaylistPageActions pageActions;
    Ui::DrawPlaylistPage(store_, currentPlaylist(), currentSongs(), playingSongId_, selectMode_,
                         selectedSongIds_, io.DisplaySize.x - sidebarWidth, mainHeight, &pageActions,
                         nullptr, false);
    ImGui::EndChild();

    // ---------------- 播放条 ----------------
    ImGui::SetCursorPos(ImVec2(0, io.DisplaySize.y - playerHeight));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Ui::Vec4(Ui::Colors::BgPlayer));
    ImGui::BeginChild("##playbar", ImVec2(io.DisplaySize.x, playerHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    Ui::PlayBarData playBarData;
    playBarData.playing = playing_;
    playBarData.hasMedia = hasMedia_;
    playBarData.positionMs = positionMs_;
    playBarData.durationMs = durationMs_;
    playBarData.volume = volume_;
    playBarData.mode = mode_;
    playBarData.title = playingTitle_.empty() ? "还没有在播放" : playingTitle_;
    playBarData.artist = playingArtist_.empty() ? "选一个歌单，或粘贴 BV 号" : playingArtist_;
    if (const StoredSong* currentSong = findSong(playingSongId_)) {
        playBarData.coverUrl = currentSong->coverUrl;
    }
    if (playBarData.coverUrl.empty()) {
        if (const StoredPlaylist* playlist = currentPlaylist()) {
            playBarData.coverUrl = playlist->coverUrl;
            playBarData.coverPath = playlist->coverPath;
        }
    }
    playBarData.showQuality = currentIsOnline_;
    playBarData.qualityId = qualityId_;
    playBarData.qualityLabel =
        qualityNameForId(lastStreamQualityId_ > 0 ? lastStreamQualityId_ : qualityId_);
    Ui::PlayBarActions playBarActions;
    Ui::DrawPlayBar(playBarData, &playBarActions, io.DisplaySize.x, playerHeight);
    ImGui::EndChild();

    drawDialogs();

    ImGui::End();

    // ================= 动作分发 =================
    if (sidebarActions.selectPlaylist) selectPlaylist(sidebarActions.playlistId);
    if (sidebarActions.createPlaylist) openCreatePlaylistDialog();
    if (sidebarActions.pasteLink) linkDialogRequested_ = true;
    if (sidebarActions.login) loginDialogRequested_ = true;
    if (sidebarActions.logout) {
        bili_.clearCookie();
        accountLoggedIn_ = false;
        accountName_.clear();
        accountId_ = 0;
        setStatus("已退出登录");
    }
    if (sidebarActions.openOutputDevice) setStatus("输出设备选择：M4 实现");
    if (sidebarActions.openLocalMusic) {
        const StoredPlaylist* local = store_.findBySource("LOCAL", "本地音乐");
        if (local == nullptr) {
            for (const StoredPlaylist& playlist : store_.playlists()) {
                if (playlist.title == "本地音乐") {
                    local = &playlist;
                    break;
                }
            }
        }
        if (local == nullptr) {
            scanLocalMusic();
        } else {
            selectPlaylist(local->id);
            setStatus("本地音乐：点右上角「…」→ 扫描本地音乐文件夹");
        }
    }
    if (sidebarActions.toggleImportMenu) importSourceDialogRequested_ = true;

    if (pageActions.playAll) playFirstPlayable();
    if (pageActions.playSongId >= 0) playSongById(pageActions.playSongId);
    if (pageActions.toggleSelectMode) {
        selectMode_ = !selectMode_;
        if (!selectMode_) selectedSongIds_.clear();
    }
    if (pageActions.toggleSong) {
        if (selectedSongIds_.count(pageActions.toggleSongId) != 0) {
            selectedSongIds_.erase(pageActions.toggleSongId);
        } else {
            selectedSongIds_.insert(pageActions.toggleSongId);
        }
    }
    if (pageActions.selectAll) {
        for (const StoredSong* song : currentSongs()) selectedSongIds_.insert(song->id);
    }
    if (pageActions.clearSelection) selectedSongIds_.clear();
    if (pageActions.copySelected) {
        if (selectedSongIds_.empty()) {
            setStatus("先勾选要复制的曲目");
        } else {
            multiAction_ = 1;
            targetPlaylistDialogRequested_ = true;
        }
    }
    if (pageActions.moveSelected) {
        if (selectedSongIds_.empty()) {
            setStatus("先勾选要移动的曲目");
        } else {
            multiAction_ = 2;
            targetPlaylistDialogRequested_ = true;
        }
    }
    if (pageActions.deleteSelected) {
        if (selectedSongIds_.empty()) {
            setStatus("先勾选要删除的曲目");
        } else {
            deleteSongsDialogRequested_ = true;
        }
    }
    if (pageActions.pickCoverFromSong) coverSongDialogRequested_ = true;
    if (pageActions.pickCoverFromFile) chooseLocalCover();
    if (pageActions.scanLocalMusic) scanLocalMusic();
    if (pageActions.selectMode) setStatus("多选 / 复制 / 移动：M3 后半段实现");
    if (pageActions.requestRename) openRenamePlaylistDialog();
    if (pageActions.requestDelete) openDeletePlaylistDialog();
    if (pageActions.openMenu) setStatus("封面 / 分享：M3 后半段实现");

    if (playBarActions.togglePlay) togglePlayback();
    if (playBarActions.previous) playPrevious();
    if (playBarActions.next) playNext();
    if (playBarActions.cycleMode) {
        mode_ = (mode_ + 1) % 4;
        setStatus("播放模式：" + formatModeName(mode_));
    }
    if (playBarActions.seekFinished) engine_.seekMs((int64_t)playBarActions.seekMs);
    if (playBarActions.volumeWheelDelta != 0.0f) {
        // 滚轮微调：每格 1%
        int next = volume_ + (int)std::lround(playBarActions.volumeWheelDelta * 1.0f);
        volume_ = next < 0 ? 0 : (next > 100 ? 100 : next);
        engine_.setVolume(volume_);
    }
    if (playBarActions.volumeChanged || playBarActions.volumeFinished) {
        volume_ = playBarActions.volume;
        engine_.setVolume(volume_);
    }
    if (playBarActions.qualitySelected) applyQualitySelection(playBarActions.qualityId);
    if (playBarActions.download) startDownloadCurrent();
    if (playBarActions.openQueue) setStatus("播放队列：M4 实现");
    if (playBarActions.openSleepTimer) setStatus("定时关闭：M4 实现");
    if (playBarActions.openOutputDevice) setStatus("输出设备：M4 实现");
}
