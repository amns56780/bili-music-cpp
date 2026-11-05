#include "app/App.h"

#include <algorithm>
#include <array>
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

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <commdlg.h>
#include <shlobj.h>

#include "media/AudioSpectrum.h"
#include "net/HttpClient.h"
#include "ui/Components.h"
#include "ui/ImageCache.h"
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

// ---------------- 整首播放缓存 ----------------
std::wstring utf8ToWide(const std::string& text);  // 定义在下方

std::wstring playbackCacheDir() { return LibraryStore::dataDir() + L"\\cache"; }

std::wstring playbackCacheFile(const std::string& bvid, int qualityKey) {
    return playbackCacheDir() + L"\\" + utf8ToWide(bvid) + L"_" + std::to_wstring(qualityKey) +
           L".m4s";
}

bool cacheFileUsable(const std::wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(handle, &size) != FALSE;
    CloseHandle(handle);
    return ok && size.QuadPart > 100 * 1024;
}

void removePlaybackCacheFiles() {
    const std::wstring dir = playbackCacheDir();
    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        DeleteFileW((dir + L"\\" + data.cFileName).c_str());
    } while (FindNextFileW(handle, &data) != FALSE);
    FindClose(handle);
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
    AppLog::info("App", std::string("bilimusic v") + BILIMUSIC_VERSION + " 启动");
    Ui::InitStyle();
    Ui::InitFonts();
    bili_.init();
    qualityId_ = qualityIdForKey(bili_.getSetting("audio_quality"));

    std::string engineError;
    if (!engine_.init(&engineError)) {
        setStatus("VLC 内核加载失败：" + engineError, true);
    }
    selectedOutputDeviceId_ = bili_.getSetting("audio.outputDevice");
    if (!selectedOutputDeviceId_.empty()) engine_.setAudioOutputDevice(selectedOutputDeviceId_);

    // 播放缓存范围（秒）：没有配置时默认 5 秒（seek 更快；旧版本是写死 20 秒）
    {
        const std::string cacheSetting = bili_.getSetting("audio.network_cache");
        if (!cacheSetting.empty()) {
            try {
                networkCacheSeconds_ = std::max(1, std::min(60, std::stoi(cacheSetting)));
            } catch (const std::exception&) {
            }
        }
        engine_.setNetworkCacheMs(networkCacheSeconds_ * 1000);
    }
    sidebarCollapsed_ = bili_.getSetting("ui.sidebar_collapsed") == "1";
    sidebarAnimWidth_ = sidebarCollapsed_ ? 0.0f : (float)Ui::Dimens::SidebarWidth;
    sidebarActionsExpanded_ = bili_.getSetting("ui.sidebar_actions_expanded") != "0";
    fullCacheEnabled_ = bili_.getSetting("audio.full_cache") != "0";

    // 恢复音量与窗口位置/大小（Java 版同款 key）
    const std::string savedVolume = bili_.getSetting("desktop.volume");
    if (!savedVolume.empty()) {
        try {
            volume_ = std::max(0, std::min(100, std::stoi(savedVolume)));
        } catch (const std::exception&) {
        }
    }
    engine_.setVolume(volume_);  // 让引擎/播放条都从保存值开始
    if (window_ != nullptr) {
        const std::string geometry = bili_.getSetting("window.geometry");
        int x = 0, y = 0, w = 0, h = 0;
        if (std::sscanf(geometry.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w >= 900 &&
            h >= 600 && x > -30000 && y > -30000) {
            glfwSetWindowSize(window_, w, h);
            glfwSetWindowPos(window_, x, y);
        }
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

void App::saveWindowState() {
    if (window_ == nullptr || immersiveFullscreen_) return;
    // 用 Win32 的真实窗口矩形：GLFW 的缓存可能在外部 SetWindowPos/拖动后还没刷新
    int x = 0, y = 0, w = 0, h = 0;
    HWND hwnd = glfwGetWin32Window(window_);
    RECT rect{};
    if (hwnd != nullptr && GetWindowRect(hwnd, &rect) != FALSE) {
        x = rect.left;
        y = rect.top;
        w = rect.right - rect.left;
        h = rect.bottom - rect.top;
    } else {
        glfwGetWindowPos(window_, &x, &y);
        glfwGetWindowSize(window_, &w, &h);
    }
    if (x > -30000 && y > -30000 && w > 300 && h > 300) {
        bili_.setSetting("window.geometry", std::to_string(x) + "," + std::to_string(y) + "," +
                                               std::to_string(w) + "," + std::to_string(h));
    }
    bili_.setSetting("desktop.volume", std::to_string(volume_));
}

void App::shutdown() {
    media::AudioSpectrum::stop();
    saveWindowState();
    engine_.shutdown();
}

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
    autoNextHandledSongId_ = -1;
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

    // 整首缓存命中 → 直接本地播放（seek 秒响应、不吃网络）
    const std::wstring fullCache = playbackCacheFile(bvid, qualityIdSnapshot);
    if (cacheFileUsable(fullCache)) {
        std::string error;
        if (engine_.playPath(fullCache, &error)) {
            engine_.setVolume(volume_);
            currentIsOnline_ = false;
            pendingSongId_ = -1;
            curStreamUrl_.clear();
            curBackups_.clear();
            lastStreamQualityId_ = qualityIdSnapshot;
            stallWatchStartTick_ = 0;
            backupIndex_ = 0;
            if (pendingSeekMs_ >= 0.0) {
                engine_.seekMs((int64_t)pendingSeekMs_);
                pendingSeekMs_ = -1.0;
            }
            setStatus("正在播放（整首缓存）：" + title);
            AppLog::info("App", "整首缓存命中，本地播放 bvid=" + bvid);
            prefetchNextSongCache();
            return;
        }
    }
    // 后台把当前歌曲整首缓存下来（流播放同时进行，下载完自动切本地）
    startCacheDownload(songId, bvid, cid, qualityIdSnapshot, title);

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
    autoNextHandledSongId_ = -1;
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

void App::toggleSidebar() {
    sidebarCollapsed_ = !sidebarCollapsed_;
    bili_.setSetting("ui.sidebar_collapsed", sidebarCollapsed_ ? "1" : "0");
    AppLog::info("App", sidebarCollapsed_ ? "折叠左侧栏" : "展开左侧栏");
}

void App::seekRelative(double seconds) {
    if (!hasMedia_ || durationMs_ <= 0.0) return;
    const double base = seekPreviewMs_ >= 0.0 ? seekPreviewMs_ : positionMs_;
    double target = base + seconds * 1000.0;
    if (target < 0.0) target = 0.0;
    if (target > durationMs_) target = durationMs_ > 1000.0 ? durationMs_ - 1000.0 : 0.0;
    engine_.seekMs((int64_t)target);
    requestSeekPreview(target);
    AppLog::info("App", std::string(seconds < 0 ? "快退 " : "快进 ") +
                            std::to_string((int)std::abs(seconds)) + " 秒");
    setStatus(std::string(seconds < 0 ? "快退 " : "快进 ") +
              std::to_string((int)std::abs(seconds)) + " 秒");
}

void App::requestSeekPreview(double positionMs) {
    seekPreviewMs_ = positionMs;
    seekPreviewUntil_ = GetTickCount64() + 1500;
    positionMs_ = positionMs;
}

void App::tick() {
    pollAsync();
    syncPlaybackState();
    checkSleepTimer();
    checkTrackEnd();
}

void App::checkTrackEnd() {
    if (!hasMedia_ || playingSongId_ < 0) return;
    if (autoNextHandledSongId_ == playingSongId_) return;
    const bool ended = engine_.ended();
    // 兜底：有些格式 ended 状态不触发，位置停在（接近）总时长且不是用户暂停
    const bool nearEnd = !engine_.userPaused() && !playing_ && durationMs_ > 1000.0 &&
                         positionMs_ >= durationMs_ - 1000.0;
    if (!ended && !nearEnd) return;
    autoNextHandledSongId_ = playingSongId_;
    AppLog::info("App", "播放结束，自动下一首：" + playingTitle_);
    setStatus("播放结束，自动播放下一首");
    stepSong(1);
}

void App::syncPlaybackState() {
    hasMedia_ = engine_.ready() && engine_.lengthMs() > 0;
    positionMs_ = engine_.ready() ? (double)engine_.positionMs() : 0.0;
    durationMs_ = engine_.ready() ? (double)engine_.lengthMs() : 0.0;
    const bool enginePlaying = engine_.ready() && engine_.isPlaying();
    if (GetTickCount64() >= playbackOverrideUntil_) {
        playing_ = enginePlaying;
    }
    engine_.pumpVolume();
    volume_ = engine_.volume();
    // seek 预览：松手后用目标位置显示，等引擎位置追上（或超时）再切回真实位置
    if (seekPreviewMs_ >= 0.0) {
        const bool caughtUp = std::abs(positionMs_ - seekPreviewMs_) < 1500.0;
        if (caughtUp || GetTickCount64() > seekPreviewUntil_) {
            seekPreviewMs_ = -1.0;
        } else {
            positionMs_ = seekPreviewMs_;
        }
    }
    checkPlaybackStall();
}

void App::startSleepTimer(int minutes) {
    if (minutes <= 0) {
        cancelSleepTimer();
        return;
    }
    sleepTimerActive_ = true;
    sleepTimerMinutes_ = minutes;
    sleepTimerEndTick_ = GetTickCount64() + (uint64_t)minutes * 60000ULL;
    setStatus("定时关闭：将在 " + std::to_string(minutes) + " 分钟后暂停播放");
}

void App::cancelSleepTimer() {
    if (!sleepTimerActive_) return;
    sleepTimerActive_ = false;
    sleepTimerEndTick_ = 0;
    sleepTimerMinutes_ = 0;
    setStatus("已取消定时关闭");
}

void App::checkSleepTimer() {
    if (!sleepTimerActive_) return;
    if (GetTickCount64() < sleepTimerEndTick_) return;
    sleepTimerActive_ = false;
    sleepTimerEndTick_ = 0;
    sleepTimerMinutes_ = 0;
    if (engine_.isPlaying()) engine_.pause();
    stallWatchStartTick_ = 0;
    setStatus("定时关闭：已暂停播放");
    AppLog::info("App", "定时关闭触发，已暂停播放");
}

void App::refreshOutputDevices() {
    std::vector<std::pair<std::string, std::string>> devices;
    for (const VlcEngine::AudioDevice& device : engine_.audioOutputDevices()) {
        devices.emplace_back(device.id, device.name);
    }
    if (devices.empty() && !outputDevices_.empty()) return;  // 还没起播时保留上次枚举结果
    outputDevices_ = std::move(devices);
    AppLog::info("App", "音频输出设备：" + std::to_string(outputDevices_.size()) + " 个");
}

void App::applyOutputDevice(const std::string& id) {
    selectedOutputDeviceId_ = id;
    engine_.setAudioOutputDevice(id);
    engine_.applyOutputDevice();
    bili_.setSetting("audio.outputDevice", id);
    std::string name = "默认设备";
    for (const auto& device : outputDevices_) {
        if (device.first == id) name = device.second;
    }
    setStatus("输出设备：" + name);
    AppLog::info("App", "切换输出设备：" + name + " (" + id + ")");
}

namespace {

ImU32 fadeImColor(ImU32 color, float alpha) {
    const ImU32 a = (ImU32)(((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

}  // namespace

void App::openImmersive() {
    if (immersiveOpen_ && !immersiveClosing_) return;
    immersiveOpen_ = true;
    immersiveClosing_ = false;
    immersiveAnim_ = 0.0f;
    immersiveEnteredTick_ = GetTickCount64();
    immersiveVolumeOpen_ = false;
    immersiveBgMenuOpen_ = false;
    immersiveModeTip_.clear();
    media::AudioSpectrum::start();  // 沉浸模式期间才采集频谱（对齐 Java 版）
    AppLog::info("App", "打开沉浸模式（自下而上滑入）");
}

void App::closeImmersive() {
    if (!immersiveOpen_) return;
    if (immersiveFullscreen_) {
        glfwSetWindowMonitor(window_, nullptr, savedWindowX_, savedWindowY_, savedWindowW_,
                             savedWindowH_, GLFW_DONT_CARE);
        immersiveFullscreen_ = false;
    }
    immersiveClosing_ = true;
    immersiveVolumeOpen_ = false;
    immersiveBgMenuOpen_ = false;
    AppLog::info("App", "关闭沉浸模式");
}

void App::toggleImmersiveFullscreen() {
    if (window_ == nullptr) return;
    if (!immersiveFullscreen_) {
        glfwGetWindowPos(window_, &savedWindowX_, &savedWindowY_);
        glfwGetWindowSize(window_, &savedWindowW_, &savedWindowH_);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor == nullptr) return;
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode == nullptr) return;
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        immersiveFullscreen_ = true;
        AppLog::info("App", "沉浸模式：进入全屏");
    } else {
        glfwSetWindowMonitor(window_, nullptr, savedWindowX_, savedWindowY_, savedWindowW_,
                             savedWindowH_, GLFW_DONT_CARE);
        immersiveFullscreen_ = false;
        AppLog::info("App", "沉浸模式：退出全屏");
    }
}

void App::pickImmersiveImage() {
    const std::wstring path = chooseImageFileW();
    if (path.empty()) return;
    immersiveBgPath_ = wideToUtf8(path);
    immersiveBgMode_ = 1;
    AppLog::info("App", "沉浸模式自定义背景：" + immersiveBgPath_);
}

void App::drawImmersive() {
    const ImGuiIO& io = ImGui::GetIO();
    const float delta = io.DeltaTime > 0.0f ? io.DeltaTime : 0.016f;
    if (immersiveClosing_) {
        immersiveAnim_ = std::max(0.0f, immersiveAnim_ - delta * 4.5f);
    } else if (immersiveOpen_) {
        immersiveAnim_ = std::min(1.0f, immersiveAnim_ + delta * 4.5f);
    } else {
        return;
    }
    const float anim = immersiveAnim_;
    if (anim <= 0.0f) {
        immersiveOpen_ = false;
        immersiveClosing_ = false;
        media::AudioSpectrum::stop();
        return;
    }
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const float slideOffset = (1.0f - anim) * height;
    const float scale = std::min(width / 1240.0f, height / 860.0f);
    auto px = [scale](float value) { return value * scale; };
    auto fade = [anim](ImU32 color) { return fadeImColor(color, anim); };

    // 沉浸模式期间不创建侧栏/主内容/播放条等子窗口：直接在 root 窗口里绘制，
    // 这样整屏的鼠标命中都落在 root 上，按钮/滑条才能正常点击。
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 铺满整屏的点击拦截层（允许后续控件覆盖/命中）
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##immersive_blocker", ImVec2(width, height));

    const StoredSong* song = findSong(playingSongId_);
    std::string coverUrl;
    std::string coverPath;
    if (song != nullptr) coverUrl = song->coverUrl;
    if (const StoredPlaylist* playlist = currentPlaylist()) {
        if (coverUrl.empty()) {
            coverUrl = playlist->coverUrl;
            coverPath = playlist->coverPath;
        }
    }
    const std::string currentTitle = playingTitle_.empty() ? "还没有在播放" : playingTitle_;
    const std::string currentArtist = playingArtist_.empty() ? "未知UP主" : playingArtist_;

    // ---------------- 背景 ----------------
    ImTextureID bgTexture = (ImTextureID)0;
    if (immersiveBgMode_ == 0) {
        bgTexture = Ui::BlurredWallpaperTexture();
    } else if (immersiveBgMode_ == 1) {
        bgTexture = Ui::BlurredImageTexture(immersiveBgPath_);
    }
    const ImVec2 bgMin(0.0f, slideOffset);
    const ImVec2 bgMax(width, slideOffset + height);
    if (bgTexture != 0) {
        int texW = 0;
        int texH = 0;
        Ui::CoverTextureSize(bgTexture, &texW, &texH);
        ImVec2 uv0(0.0f, 0.0f);
        ImVec2 uv1(1.0f, 1.0f);
        if (texW > 0 && texH > 0) {
            const float screenAspect = width / height;
            const float textureAspect = (float)texW / (float)texH;
            if (textureAspect > screenAspect) {
                const float visible = screenAspect / textureAspect;
                uv0.x = (1.0f - visible) * 0.5f;
                uv1.x = uv0.x + visible;
            } else {
                const float visible = textureAspect / screenAspect;
                uv0.y = (1.0f - visible) * 0.5f;
                uv1.y = uv0.y + visible;
            }
        }
        draw->AddImage(bgTexture, bgMin, bgMax, uv0, uv1, fade(IM_COL32(255, 255, 255, 255)));
    } else {
        draw->AddRectFilledMultiColor(bgMin, bgMax, fade(IM_COL32(0x1B, 0x1B, 0x22, 255)),
                                      fade(IM_COL32(0x1B, 0x1B, 0x22, 255)),
                                      fade(IM_COL32(0x0A, 0x0A, 0x0C, 255)),
                                      fade(IM_COL32(0x0A, 0x0A, 0x0C, 255)));
    }
    const float midY = bgMin.y + height * 0.5f;
    draw->AddRectFilledMultiColor(bgMin, ImVec2(width, midY), fade(IM_COL32(0, 0, 0, 0x99)),
                                  fade(IM_COL32(0, 0, 0, 0x99)), fade(IM_COL32(0, 0, 0, 0x44)),
                                  fade(IM_COL32(0, 0, 0, 0x44)));
    draw->AddRectFilledMultiColor(ImVec2(0.0f, midY), bgMax, fade(IM_COL32(0, 0, 0, 0x44)),
                                  fade(IM_COL32(0, 0, 0, 0x44)), fade(IM_COL32(0, 0, 0, 0xC8)),
                                  fade(IM_COL32(0, 0, 0, 0xC8)));

    // ---------------- 本地控件（前景 drawlist 版本）----------------
    auto roundButton = [&](const char* id, uint32_t icon, float x, float y, float size,
                           float iconSize, bool active, ImU32 background) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemAllowOverlap();
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##btn", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 center(x + size * 0.5f, y + size * 0.5f);
        ImU32 bgColor = background;
        if (hovered) {
            bgColor = background == IM_COL32(0, 0, 0, 0)
                          ? IM_COL32(255, 255, 255, 0x22)
                          : IM_COL32(0xEC, 0x41, 0x41, 0xDD);
        }
        if (bgColor != IM_COL32(0, 0, 0, 0)) draw->AddCircleFilled(center, size * 0.5f, fade(bgColor), 32);
        const std::string glyph = Icons::Utf8(icon);
        draw->AddText(Ui::GetFonts().body, iconSize,
                      ImVec2(center.x - iconSize * 0.5f, center.y - iconSize * 0.325f),
                      fade(hovered || active ? IM_COL32(255, 255, 255, 255)
                                             : IM_COL32(255, 255, 255, 220)),
                      glyph.c_str());
        return clicked;
    };
    auto playButton = [&](const char* id, float centerX, float centerY, float size) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(centerX - size * 0.5f, centerY - size * 0.5f));
        ImGui::SetNextItemAllowOverlap();
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##play", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddCircleFilled(ImVec2(centerX, centerY), size * 0.5f,
                              fade(hovered ? IM_COL32(0xF0, 0x52, 0x52, 255)
                                           : IM_COL32(0xEC, 0x41, 0x41, 255)),
                              48);
        const ImU32 white = fade(IM_COL32(255, 255, 255, 255));
        if (playing_) {
            // 两条竖杠间隙收窄（用户反馈太宽）
            const float barWidth = size * 0.095f;
            const float barHeight = size * 0.31f;
            const float gap = size * 0.052f;
            draw->AddRectFilled(ImVec2(centerX - gap - barWidth, centerY - barHeight * 0.5f),
                                ImVec2(centerX - gap, centerY + barHeight * 0.5f), white, 1.0f);
            draw->AddRectFilled(ImVec2(centerX + gap, centerY - barHeight * 0.5f),
                                ImVec2(centerX + gap + barWidth, centerY + barHeight * 0.5f), white,
                                1.0f);
        } else {
            // 三角形重心居中（用户反馈偏右，apex 往左收一点）
            const float half = size * 0.155f;
            const ImVec2 a(centerX - half * 0.6f + size * 0.025f, centerY - half);
            const ImVec2 b(centerX - half * 0.6f + size * 0.025f, centerY + half);
            const ImVec2 c(centerX + half * 0.85f, centerY);
            draw->AddTriangleFilled(a, b, c, white);
        }
        return clicked;
    };
    auto coverImage = [&](const std::string& seed, float x, float y, float size, float radius) {
        const ImTextureID texture = Ui::CoverTextureFor(coverUrl, coverPath);
        if (texture == 0) {
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size),
                                fade(IM_COL32(0x33, 0x33, 0x3D, 255)), radius);
            const std::string glyph = Icons::Utf8(Icons::MusicNote);
            const float textSize = size * 0.30f;
            const float textWidth = Ui::MeasureText(Ui::GetFonts().xl, textSize, glyph);
            draw->AddText(Ui::GetFonts().xl, textSize,
                          ImVec2(x + (size - textWidth) * 0.5f, y + size * 0.5f - textSize * 0.4f),
                          fade(IM_COL32(255, 255, 255, 235)), glyph.c_str());
            return;
        }
        int texW = 0;
        int texH = 0;
        Ui::CoverTextureSize(texture, &texW, &texH);
        ImVec2 uv0(0, 0);
        ImVec2 uv1(1, 1);
        if (texW > 0 && texH > 0) {
            if (texW > texH) {
                const float visible = (float)texH / (float)texW;
                uv0.x = (1.0f - visible) * 0.5f;
                uv1.x = uv0.x + visible;
            } else if (texH > texW) {
                const float visible = (float)texW / (float)texH;
                uv0.y = (1.0f - visible) * 0.5f;
                uv1.y = uv0.y + visible;
            }
        }
        draw->AddImageRounded(texture, ImVec2(x, y), ImVec2(x + size, y + size), uv0, uv1,
                              fade(IM_COL32(255, 255, 255, 255)), radius);
    };
    // 返回 true 表示松手（finished），changed 通过引用返回
    auto slider = [&](const char* id, float x, float y, float width, float* value,
                      float barHeight, bool* changed) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemAllowOverlap();
        ImGui::PushID(id);
        ImGui::InvisibleButton("##slider", ImVec2(width, px(14)));
        const bool active = ImGui::IsItemActive();
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (active) {
            const float mouseX = io.MousePos.x;
            float ratio = (mouseX - x) / width;
            ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
            if (ratio != *value) {
                *value = ratio;
                *changed = true;
            }
        }
        const float centerY = y + px(14) * 0.5f;
        draw->AddRectFilled(ImVec2(x, centerY - barHeight * 0.5f),
                            ImVec2(x + width, centerY + barHeight * 0.5f),
                            fade(IM_COL32(255, 255, 255, 0x40)), barHeight * 0.5f);
        const float played = x + width * (*value);
        if (played > x) {
            draw->AddRectFilled(ImVec2(x, centerY - barHeight * 0.5f),
                                ImVec2(played, centerY + barHeight * 0.5f),
                                fade(IM_COL32(0xEC, 0x41, 0x41, 255)), barHeight * 0.5f);
        }
        if (hovered || active) {
            draw->AddCircleFilled(ImVec2(played, centerY), px(5),
                                  fade(IM_COL32(255, 255, 255, 255)), 16);
        }
        return deactivated;
    };

    // ---------------- 左上角：时间 / 日期 ----------------
    const std::time_t nowTime = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);
    static const char* kWeekNames[] = {"星期日", "星期一", "星期二", "星期三",
                                       "星期四", "星期五", "星期六"};
    char clockText[16];
    std::snprintf(clockText, sizeof(clockText), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
    char dateText[48];
    std::snprintf(dateText, sizeof(dateText), "%d月%d日 %s", localTime.tm_mon + 1,
                  localTime.tm_mday, kWeekNames[localTime.tm_wday % 7]);
    draw->AddText(Ui::GetFonts().xl, px(42), ImVec2(px(28), slideOffset + px(22)),
                  fade(IM_COL32(255, 255, 255, 255)), clockText);
    draw->AddText(Ui::GetFonts().small, px(15), ImVec2(px(28), slideOffset + px(74)),
                  fade(IM_COL32(255, 255, 255, 0x99)), dateText);

    // ---------------- 右上角按钮 ----------------
    const float btnSize = px(40);
    const float btnGap = px(8);
    const float btnY = slideOffset + px(20);
    const float rightEdge = width - px(20);
    const float closeX = rightEdge - btnSize;
    const float settingsX = closeX - btnGap - btnSize;
    const float volumeX = settingsX - btnGap - btnSize;
    const float modeX = volumeX - btnGap - btnSize;
    const float fullscreenX = modeX - btnGap - btnSize;
    uint32_t modeIcon = Icons::PlaylistPlay;
    if (mode_ == 1) modeIcon = Icons::Repeat;
    else if (mode_ == 2) modeIcon = Icons::RepeatOne;
    else if (mode_ == 3) modeIcon = Icons::Shuffle;
    const uint32_t volumeIcon = volume_ <= 0 ? Icons::VolumeOff
                                             : (volume_ < 50 ? Icons::VolumeDown : Icons::VolumeUp);
    const ImU32 buttonBackground = IM_COL32(0, 0, 0, 0x55);
    if (roundButton("imm_fullscreen", immersiveFullscreen_ ? Icons::FullscreenExit : Icons::Fullscreen,
                    fullscreenX, btnY, btnSize, px(20), immersiveFullscreen_, buttonBackground)) {
        toggleImmersiveFullscreen();
    }
    if (roundButton("imm_mode", modeIcon, modeX, btnY, btnSize, px(20), false, buttonBackground)) {
        mode_ = (mode_ + 1) % 4;
        immersiveModeTip_ = formatModeName(mode_);
        immersiveModeTipUntil_ = GetTickCount64() + 2000;
        setStatus("播放模式：" + immersiveModeTip_);
    }
    if (roundButton("imm_volume", volumeIcon, volumeX, btnY, btnSize, px(20), immersiveVolumeOpen_,
                    buttonBackground)) {
        immersiveVolumeOpen_ = !immersiveVolumeOpen_;
        immersiveBgMenuOpen_ = false;
    }
    if (roundButton("imm_bg", Icons::Settings, settingsX, btnY, btnSize, px(20), immersiveBgMenuOpen_,
                    buttonBackground)) {
        immersiveBgMenuOpen_ = !immersiveBgMenuOpen_;
        immersiveVolumeOpen_ = false;
    }
    if (roundButton("imm_close", Icons::Close, closeX, btnY, btnSize, px(20), false,
                    buttonBackground)) {
        closeImmersive();
    }

    // ---------------- 音量弹层 ----------------
    if (immersiveVolumeOpen_) {
        const float panelW = px(280);
        const float panelH = px(46);
        const ImVec2 pMin(width - px(20) - panelW, slideOffset + px(72));
        const ImVec2 pMax(pMin.x + panelW, pMin.y + panelH);
        draw->AddRectFilled(pMin, pMax, fade(IM_COL32(0x1C, 0x1C, 0x22, 0xEE)), px(12));
        const std::string glyph = Icons::Utf8(volumeIcon);
        draw->AddText(Ui::GetFonts().body, px(18),
                      ImVec2(pMin.x + px(14), pMin.y + panelH * 0.5f - px(18) * 0.325f),
                      fade(IM_COL32(255, 255, 255, 255)), glyph.c_str());
        float value = volume_ / 100.0f;
        bool volumeChanged = false;
        const bool volumeFinished =
            slider("##imm_volume_slider", pMin.x + px(46), pMin.y + panelH * 0.5f - px(7),
                   panelW - px(46) - px(52), &value, px(4), &volumeChanged);
        if (volumeChanged) {
            volume_ = (int)std::lround(value * 100.0f);
            volume_ = volume_ < 0 ? 0 : (volume_ > 100 ? 100 : volume_);
            engine_.setVolume(volume_);
        }
        if (volumeFinished) bili_.setSetting("desktop.volume", std::to_string(volume_));
        char volumeText[8];
        std::snprintf(volumeText, sizeof(volumeText), "%d", volume_);
        const float volumeTextWidth = Ui::MeasureText(Ui::GetFonts().small, px(15), volumeText);
        draw->AddText(Ui::GetFonts().small, px(15),
                      ImVec2(pMax.x - px(14) - volumeTextWidth,
                             pMin.y + panelH * 0.5f - px(15) * 0.55f),
                      fade(IM_COL32(255, 255, 255, 0xCC)), volumeText);
    }

    // ---------------- 背景设置弹层 ----------------
    if (immersiveBgMenuOpen_) {
        const float menuW = px(200);
        const float rowH = px(34);
        const ImVec2 mMin(width - px(20) - menuW, slideOffset + px(72));
        const float menuH = rowH * 5 + px(1) + px(12);
        draw->AddRectFilled(mMin, ImVec2(mMin.x + menuW, mMin.y + menuH),
                            fade(IM_COL32(0x1C, 0x1C, 0x22, 0xEE)), px(12));
        static const char* kBgLabels[3] = {"桌面壁纸", "自定义图片", "纯色"};
        for (int i = 0; i < 3; ++i) {
            const float rowY = mMin.y + px(6) + rowH * i;
            ImGui::SetCursorScreenPos(ImVec2(mMin.x, rowY));
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##bg_row", ImVec2(menuW, rowH));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hovered) {
                draw->AddRectFilled(ImVec2(mMin.x, rowY), ImVec2(mMin.x + menuW, rowY + rowH),
                                    fade(IM_COL32(255, 255, 255, 0x14)));
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            const bool selected = immersiveBgMode_ == i;
            draw->AddText(Ui::GetFonts().body, px(16),
                          ImVec2(mMin.x + px(14), rowY + rowH * 0.5f - px(16) * 0.325f),
                          fade(selected ? IM_COL32(0xEC, 0x41, 0x41, 255)
                                        : IM_COL32(255, 255, 255, 0xE6)),
                          kBgLabels[i]);
            if (selected) {
                draw->AddCircleFilled(ImVec2(mMin.x + menuW - px(16), rowY + rowH * 0.5f), px(3),
                                      fade(IM_COL32(0xEC, 0x41, 0x41, 255)));
            }
            if (clicked) {
                immersiveBgMode_ = i;
                if (i == 1) pickImmersiveImage();
                immersiveBgMenuOpen_ = false;
            }
        }
        const float dividerY = mMin.y + px(6) + rowH * 3;
        draw->AddLine(ImVec2(mMin.x, dividerY), ImVec2(mMin.x + menuW, dividerY),
                      fade(IM_COL32(255, 255, 255, 0x33)), 1.0f);
        const float waveRowY = dividerY + px(1);
        ImGui::SetCursorScreenPos(ImVec2(mMin.x, waveRowY));
        ImGui::PushID("wave_row");
        const bool waveClicked = ImGui::InvisibleButton("##wave_row", ImVec2(menuW, rowH));
        const bool waveHovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (waveHovered) {
            draw->AddRectFilled(ImVec2(mMin.x, waveRowY), ImVec2(mMin.x + menuW, waveRowY + rowH),
                                fade(IM_COL32(255, 255, 255, 0x14)));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        draw->AddText(Ui::GetFonts().body, px(16),
                      ImVec2(mMin.x + px(14), waveRowY + rowH * 0.5f - px(16) * 0.325f),
                      fade(IM_COL32(255, 255, 255, 0xE6)),
                      immersiveShowWave_ ? "隐藏律动线条" : "显示律动线条");
        if (waveClicked) {
            immersiveShowWave_ = !immersiveShowWave_;
            immersiveBgMenuOpen_ = false;
        }
    }

    // ---------------- 主内容 ----------------
    const float coverSize = px(260);
    const float coverY = slideOffset + height * 0.16f;
    coverImage(currentTitle, width * 0.5f - coverSize * 0.5f, coverY, coverSize, px(20));

    float textY = coverY + coverSize + px(34);
    const std::string& titleShown = currentTitle;
    const float titleLimit = std::min(px(800), width - px(96));
    const float titleWidth = Ui::MeasureRichText(Ui::GetFonts().xl, px(30), titleShown);
    if (titleWidth <= titleLimit) {
        Ui::DrawRichText(Ui::GetFonts().xl, px(30),
                         ImVec2(width * 0.5f - titleWidth * 0.5f, textY),
                         fade(IM_COL32(255, 255, 255, 255)), titleShown);
    } else {
        Ui::TextEllipsis(titleShown, titleLimit, Ui::GetFonts().xl, IM_COL32(255, 255, 255, 255),
                         ImVec2(width * 0.5f - titleLimit * 0.5f, textY));
    }
    textY += px(40);
    const std::string& artistShown = currentArtist;
    const float artistWidth = Ui::MeasureRichText(Ui::GetFonts().body, px(18), artistShown);
    Ui::DrawRichText(Ui::GetFonts().body, px(18),
                     ImVec2(width * 0.5f - artistWidth * 0.5f, textY),
                     fade(IM_COL32(255, 255, 255, 0xB3)), artistShown);

    // 进度条
    const float progressY = textY + px(52);
    const float progressWidth = px(560);
    static bool immersiveSeeking = false;
    static float immersiveSeekDraft = 0.0f;
    const float ratio = durationMs_ > 0.0 ? (float)(positionMs_ / durationMs_) : 0.0f;
    float sliderValue = immersiveSeeking ? immersiveSeekDraft
                                         : (ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio));
    bool seekChanged = false;
    const bool seekFinished =
        slider("##immerse_seek", width * 0.5f - progressWidth * 0.5f, progressY, progressWidth,
               &sliderValue, px(4), &seekChanged);
    if (seekChanged) {
        immersiveSeeking = true;
        immersiveSeekDraft = sliderValue;
    }
    if (seekFinished) {
        immersiveSeeking = false;
        if (durationMs_ > 0.0) {
            const double target = sliderValue * durationMs_;
            engine_.seekMs((int64_t)target);
            requestSeekPreview(target);
        }
    }
    auto formatMs = [](double ms) {
        const int64_t total = (int64_t)((ms < 0.0 ? 0.0 : ms) / 1000.0);
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", (long long)(total / 60),
                      (long long)(total % 60));
        return std::string(buffer);
    };
    const double shownPosition = immersiveSeeking ? immersiveSeekDraft * durationMs_ : positionMs_;
    const float timeRowY = progressY + px(26);
    draw->AddText(Ui::GetFonts().small, px(15),
                  ImVec2(width * 0.5f - progressWidth * 0.5f, timeRowY),
                  fade(IM_COL32(255, 255, 255, 0xCC)),
                  (formatMs(shownPosition) + " / " + formatMs(durationMs_)).c_str());
    const uint64_t nowTick = GetTickCount64();
    const uint64_t listenedMs = nowTick > immersiveEnteredTick_ ? nowTick - immersiveEnteredTick_ : 0;
    const std::time_t enteredSystemTime = nowTime - (std::time_t)(listenedMs / 1000);
    std::tm enteredTm{};
    localtime_s(&enteredTm, &enteredSystemTime);
    char enteredText[64];
    if (listenedMs < 60000) {
        std::snprintf(enteredText, sizeof(enteredText), "%02d:%02d 进入 · 已听 %d 秒",
                      enteredTm.tm_hour, enteredTm.tm_min, (int)(listenedMs / 1000));
    } else {
        std::snprintf(enteredText, sizeof(enteredText), "%02d:%02d 进入 · 已听 %d 分钟",
                      enteredTm.tm_hour, enteredTm.tm_min, (int)(listenedMs / 60000));
    }
    const float enteredWidth = Ui::MeasureText(Ui::GetFonts().small, px(15), enteredText);
    draw->AddText(Ui::GetFonts().small, px(15),
                  ImVec2(width * 0.5f + progressWidth * 0.5f - enteredWidth, timeRowY),
                  fade(IM_COL32(255, 255, 255, 0x99)), enteredText);

    // 播放控制
    const float controlsCenterY = timeRowY + px(48);
    const float playSize = px(58);
    if (roundButton("immerse_prev", Icons::SkipPrevious, width * 0.5f - px(29) - px(16) - px(56),
                    controlsCenterY - px(28), px(56), px(26), false, IM_COL32(0, 0, 0, 0))) {
        playPrevious();
    }
    if (playButton("immerse_play", width * 0.5f, controlsCenterY, playSize)) togglePlayback();
    if (roundButton("immerse_next", Icons::SkipNext, width * 0.5f + px(29) + px(16),
                    controlsCenterY - px(28), px(56), px(26), false, IM_COL32(0, 0, 0, 0))) {
        playNext();
    }

    const float tipY = controlsCenterY + playSize * 0.5f + px(10);
    if (!immersiveModeTip_.empty() && GetTickCount64() < immersiveModeTipUntil_) {
        const float tipWidth = Ui::MeasureText(Ui::GetFonts().small, px(14), immersiveModeTip_);
        draw->AddText(Ui::GetFonts().small, px(14),
                      ImVec2(width * 0.5f - tipWidth * 0.5f, tipY),
                      fade(IM_COL32(255, 255, 255, 0x99)), immersiveModeTip_.c_str());
    }

    // ---------------- 底部律动线条 ----------------
    if (immersiveShowWave_) {
        // 真实频谱：WASAPI loopback + FFT（media::AudioSpectrum）
        static float waveLevel = 0.0f;
        const float targetLevel = playing_ ? 1.0f : 0.0f;
        const float levelStep = std::min(1.0f, delta / 0.7f);  // ~700ms 过渡
        waveLevel += (targetLevel - waveLevel) * levelStep;
        const std::array<float, media::AudioSpectrum::kBands> spectrum =
            media::AudioSpectrum::bands();
        const int bars = 56;
        const int bandCount = media::AudioSpectrum::kBands;
        const float gap = px(3);
        const float areaWidth = std::min(px(700), width * 0.7f);
        const float barWidth = (areaWidth - gap * (bars - 1)) / (float)bars;
        const float left = (width - areaWidth) * 0.5f;
        const float waveHeight = px(90);
        const float waveBottom = slideOffset + height - px(26);
        for (int i = 0; i < bars; ++i) {
            const float srcPos = (float)i / (float)(bars - 1) * (float)(bandCount - 1);
            const int i0 = std::max(0, std::min(bandCount - 1, (int)srcPos));
            const int i1 = std::min(bandCount - 1, i0 + 1);
            const float frac = srcPos - (float)i0;
            const float energy =
                std::clamp(spectrum[(size_t)i0] * (1.0f - frac) + spectrum[(size_t)i1] * frac, 0.0f,
                           1.0f);
            const float amplitude = 0.06f + 0.94f * energy * waveLevel;
            const float barH = std::max(px(1), waveHeight * amplitude);
            const float x = left + i * (barWidth + gap);
            draw->AddRectFilled(ImVec2(x, waveBottom - barH), ImVec2(x + barWidth, waveBottom),
                                fadeImColor(IM_COL32(0xEC, 0x41, 0x41, 255),
                                            0.30f + 0.60f * amplitude),
                                barWidth * 0.5f);
        }
    }

    // 滚轮微调音量（沉浸模式里没有常驻音量条）
    if (io.MouseWheel != 0.0f) {
        const int next = std::max(0, std::min(100, volume_ + (int)std::lround(io.MouseWheel)));
        if (next != volume_) {
            volume_ = next;
            engine_.setVolume(volume_);
            bili_.setSetting("desktop.volume", std::to_string(volume_));
        }
    }

    // 鼠标空闲 3 秒隐藏光标
    static ImVec2 lastMousePos(-1.0f, -1.0f);
    static double lastMouseMoveTime = 0.0;
    if (io.MousePos.x != lastMousePos.x || io.MousePos.y != lastMousePos.y) {
        lastMousePos = io.MousePos;
        lastMouseMoveTime = ImGui::GetTime();
    }
    if (ImGui::GetTime() - lastMouseMoveTime > 3.0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

}

void App::togglePlayback() {
    AppLog::info("App", "切换播放/暂停（托盘或全局媒体键）");
    if (!hasMedia_) {
        playFirstPlayable();
        return;
    }
    const bool wasPlaying = engine_.isPlaying();
    engine_.togglePause();
    // 乐观 UI：立刻翻转按钮状态，避免等引擎状态同步造成的延迟感
    playing_ = !wasPlaying;
    playbackOverrideUntil_ = GetTickCount64() + 450;
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
        if (event.kind == AsyncEvent::Kind::CacheReady) {
            handleCacheReady(event);
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
        if (outputDevices_.empty()) refreshOutputDevices();  // 起播后才能枚举 mmdevice 设备
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


void App::startCacheDownload(long long songId, const std::string& bvid, long long cid,
                            int qualitySnapshot, const std::string& title, bool manual) {
    if (!manual && !fullCacheEnabled_) return;
    if (songId < 0 || bvid.empty() || cid <= 0) return;
    if (cachingSongs_.count(songId) != 0) return;
    cachingSongs_.insert(songId);
    AppLog::info("App", "开始整首缓存：" + title + " bvid=" + bvid);
    std::thread([this, songId, bvid, cid, qualitySnapshot, title]() {
        AsyncEvent event;
        event.kind = AsyncEvent::Kind::CacheReady;
        event.songId = songId;
        event.title = title;
        event.bvid = bvid;
        const bili::AudioStreamInfo stream = bili_.resolveAudio(bvid, cid, qualitySnapshot);
        if (!stream.ok) {
            event.ok = false;
            event.message = stream.message;
            postAsync(std::move(event));
            return;
        }
        net::HttpResponse response;
        std::string error;
        const std::vector<std::pair<std::string, std::string>> headers = {
            {"User-Agent", kUserAgent}, {"Referer", kReferer}};
        if (!net::httpGet(stream.url, headers, &response, &error) ||
            response.body.size() < 100 * 1024) {
            event.ok = false;
            event.message = error.empty() ? "缓存内容过小" : error;
            postAsync(std::move(event));
            return;
        }
        std::wstring directory = playbackCacheDir();
        CreateDirectoryW(directory.c_str(), nullptr);
        const std::wstring path = playbackCacheFile(bvid, qualitySnapshot);
        if (!writeBinaryFile(path, response.body)) {
            event.ok = false;
            event.message = "写入缓存失败";
            postAsync(std::move(event));
            return;
        }
        event.ok = true;
        event.filePath = wideToUtf8(path);
        event.qualityId = stream.qualityId;
        event.message = "ok";
        postAsync(std::move(event));
    }).detach();
}

void App::prefetchNextSongCache() {
    if (!fullCacheEnabled_) return;
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
    const int quality = qualityId_;
    // 只预取“当前歌之后的第一首在线歌曲”，避免把整张歌单一次性下完
    for (int step = 1; step < (int)songs.size(); ++step) {
        const StoredSong* song = songs[(size_t)((index + step) % (int)songs.size())];
        if (!song->localPath.empty()) continue;
        if (song->bvid.empty() || song->cid <= 0) continue;
        if (cacheFileUsable(playbackCacheFile(song->bvid, quality))) return;
        if (cachingSongs_.count(song->id) != 0) return;
        startCacheDownload(song->id, song->bvid, song->cid, quality, song->title);
        return;
    }
}

void App::handleCacheReady(const AsyncEvent& event) {
    cachingSongs_.erase(event.songId);
    if (!event.ok) {
        AppLog::warn("App", "整首缓存失败：" + event.title + " " + event.message);
        return;
    }
    AppLog::info("App", "整首缓存完成：" + event.title);
    const std::wstring path = utf8ToWide(event.filePath);
    // 当前正在流播的歌下载完 → 无缝换成本地文件，之后拖动进度条即时生效
    if (event.songId == playingSongId_ && currentIsOnline_ && cacheFileUsable(path)) {
        const bool wasPlaying = engine_.isPlaying();
        const double position = positionMs_;
        std::string error;
        if (engine_.playPath(path, &error)) {
            engine_.setVolume(volume_);
            if (position > 0.5 && durationMs_ > 0.0) engine_.seekMs((int64_t)position);
            if (!wasPlaying) engine_.pause();
            currentIsOnline_ = false;
            curStreamUrl_.clear();
            curBackups_.clear();
            stallWatchStartTick_ = 0;
            seekPreviewMs_ = -1.0;
            setStatus("已缓存整首《" + playingTitle_ + "》，切换本地播放（拖动进度条即时生效）");
            AppLog::info("App", "切换到整首缓存播放 bvid=" + event.bvid);
        }
    }
    // 只有「当前歌」或「当前歌的下一首」缓存完成时才继续预取一首，避免链式下载整张歌单
    long long immediateNextId = -1;
    const std::vector<const StoredSong*> songs = currentSongs();
    for (size_t i = 0; i < songs.size(); ++i) {
        if (songs[i]->id != playingSongId_) continue;
        for (size_t j = i + 1; j < songs.size(); ++j) {
            if (!songs[j]->localPath.empty() || songs[j]->bvid.empty() || songs[j]->cid <= 0) {
                continue;
            }
            immediateNextId = songs[j]->id;
            break;
        }
        break;
    }
    if (event.songId == playingSongId_ || event.songId == immediateNextId) prefetchNextSongCache();
}

void App::clearPlaybackCache() {
    removePlaybackCacheFiles();
    cachingSongs_.clear();
    setStatus("已清除播放缓存");
    AppLog::info("App", "已清除播放缓存");
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
            // 延迟 4s 再预取：起播瞬间并发取流会抢占带宽，导致当前歌曲播 1s 后断流
            std::this_thread::sleep_for(std::chrono::seconds(4));
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

    // ---- 封面：从歌单视频选（带缩略图列表 + 大图预览）----
    if (coverSongDialogRequested_) ImGui::OpenPopup("从歌单视频选封面");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("从歌单视频选封面", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!coverSongDialogRequested_) ImGui::CloseCurrentPopup();
        const StoredPlaylist* playlist = currentPlaylist();
        if (playlist == nullptr) {
            ImGui::TextDisabled("没有选中的歌单");
        } else {
            const std::vector<const StoredSong*> coverSongs = currentSongs();
            if (coverSongSelectedId_ < 0) {
                for (const StoredSong* song : coverSongs) {
                    if (!song->coverUrl.empty()) {
                        coverSongSelectedId_ = song->id;
                        break;
                    }
                }
            }
            ImGui::TextUnformatted("选择一首歌，用它的视频封面作为歌单封面：");
            ImGui::Spacing();

            // 左侧：带缩略图的歌曲列表
            ImGui::BeginChild("##cover_song_list", ImVec2(420.0f, 320.0f), ImGuiChildFlags_Borders);
            const float rowHeight = 52.0f;
            ImGui::GetWindowDrawList();
            for (const StoredSong* song : coverSongs) {
                if (song->coverUrl.empty()) continue;
                ImGui::PushID((int)song->id);
                const ImVec2 rowPos = ImGui::GetCursorScreenPos();
                const bool selected = song->id == coverSongSelectedId_;
                if (ImGui::Selectable("##cover_row", selected, ImGuiSelectableFlags_None,
                                      ImVec2(0.0f, rowHeight))) {
                    coverSongSelectedId_ = song->id;
                }
                Ui::CoverImage(song->title, ImVec2(rowPos.x + 4.0f, rowPos.y + 8.0f), 36.0f, 6.0f,
                               song->coverUrl, "");
                Ui::TextEllipsis(song->title, 340.0f, Ui::GetFonts().body,
                             selected ? Ui::Colors::Text : Ui::Colors::Text2,
                             ImVec2(rowPos.x + 50.0f, rowPos.y + 16.0f));
                ImGui::PopID();
            }
            ImGui::EndChild();

            // 右侧：选中歌曲的封面大图预览
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted("封面预览");
            const StoredSong* selectedSong = nullptr;
            for (const StoredSong* song : coverSongs) {
                if (song->id == coverSongSelectedId_) {
                    selectedSong = song;
                    break;
                }
            }
            const ImVec2 previewPos = ImGui::GetCursorScreenPos();
            const float previewSize = 220.0f;
            if (selectedSong != nullptr && !selectedSong->coverUrl.empty()) {
                Ui::CoverImage(selectedSong->title, previewPos, previewSize, 12.0f,
                               selectedSong->coverUrl, "");
            } else {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    previewPos, ImVec2(previewPos.x + previewSize, previewPos.y + previewSize),
                    Ui::Colors::TagBg, 12.0f);
                ImGui::GetWindowDrawList()->AddText(
                    Ui::GetFonts().small, 15.0f,
                    ImVec2(previewPos.x + previewSize * 0.5f - 30.0f, previewPos.y + 100.0f),
                    Ui::Colors::Text3, "暂无封面");
            }
            ImGui::Dummy(ImVec2(previewSize, previewSize));
            ImGui::EndGroup();

            ImGui::Spacing();
            long long applySongId = -1;
            if (selectedSong != nullptr) {
                ImGui::TextDisabled("已选：%s", Ui::DisplayText(selectedSong->title).c_str());
            }
            if (ImGui::Button("应用封面", ImVec2(140.0f, 32.0f))) {
                applySongId = coverSongSelectedId_;
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(110.0f, 32.0f))) {
                coverSongDialogRequested_ = false;
                ImGui::CloseCurrentPopup();
            }
            if (applySongId >= 0) {
                for (const StoredSong* song : coverSongs) {
                    if (song->id != applySongId) continue;
                    if (store_.setPlaylistCover(playlist->id, song->coverUrl, "")) {
                        setStatus("已设置歌单封面（来自歌单视频）");
                    }
                    break;
                }
                coverSongDialogRequested_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (currentPlaylist() == nullptr) {
            ImGui::Spacing();
            if (ImGui::Button("关闭", ImVec2(110.0f, 32.0f))) {
                coverSongDialogRequested_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // ---- 播放缓存设置 ----
    if (cacheSettingsDialogRequested_) ImGui::OpenPopup("播放缓存");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("播放缓存", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!cacheSettingsDialogRequested_) ImGui::CloseCurrentPopup();
        ImGui::TextUnformatted("拖动进度条后多久能听到声音，取决于网络缓存范围：");
        ImGui::TextDisabled("缓存越大越抗卡顿，但 seek 后重新出声越慢；下一首 / 重新播放后生效。");
        ImGui::Spacing();
        const int options[] = {2, 5, 10, 20};
        const int previous = networkCacheSeconds_;
        for (int seconds : options) {
            const bool selected = networkCacheSeconds_ == seconds;
            const std::string label = "缓存 " + std::to_string(seconds) + " 秒" +
                                      (seconds == 5 ? "（推荐）" : "");
            if (ImGui::Selectable(label.c_str(), selected)) {
                networkCacheSeconds_ = seconds;
                engine_.setNetworkCacheMs(seconds * 1000);
                bili_.setSetting("audio.network_cache", std::to_string(seconds));
                setStatus("播放缓存：" + std::to_string(seconds) + " 秒（下一首 / 重新播放生效）");
                cacheSettingsDialogRequested_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("整首缓存");
        bool fullCache = fullCacheEnabled_;
        if (ImGui::Checkbox("播放时自动缓存整首 + 预取下一首（推荐）", &fullCache)) {
            fullCacheEnabled_ = fullCache;
            bili_.setSetting("audio.full_cache", fullCache ? "1" : "0");
            setStatus(fullCache ? "已开启自动整首缓存" : "已关闭自动整首缓存");
            if (fullCache) prefetchNextSongCache();
        }
        ImGui::TextDisabled("缓存完成后拖动进度条即时响应；文件在 %USERPROFILE%\\.bilimusic\\cache");
        const bool hasCurrentSong = !curBvid_.empty() && curCid_ > 0;
        const bool currentCached =
            hasCurrentSong && cacheFileUsable(playbackCacheFile(curBvid_, qualityId_));
        const bool currentCaching = cachingSongs_.count(playingSongId_) != 0;
        if (hasCurrentSong) {
            ImGui::TextDisabled(currentCached ? "当前曲目：已缓存整首"
                                              : (currentCaching ? "当前曲目：正在缓存…"
                                                                : "当前曲目：未缓存"));
        } else {
            ImGui::TextDisabled("当前没有正在播放的歌曲（本地文件无需缓存）");
        }
        ImGui::BeginDisabled(!hasCurrentSong || currentCached || currentCaching);
        if (ImGui::Button("立即缓存整首当前曲目", ImVec2(210.0f, 32.0f)) && hasCurrentSong) {
            startCacheDownload(playingSongId_, curBvid_, curCid_, qualityId_, playingTitle_, true);
            setStatus("已开始缓存整首：" + playingTitle_);
        }
        ImGui::EndDisabled();
        if (currentCached) {
            ImGui::SameLine();
            ImGui::TextDisabled("（本地播放，拖动进度条即时响应）");
        }
        ImGui::Spacing();
        if (ImGui::Button("清除播放缓存", ImVec2(140.0f, 32.0f))) {
            clearPlaybackCache();
        }
        ImGui::SameLine();
        if (currentIsOnline_ && !curBvid_.empty() && curCid_ > 0) {
            if (ImGui::Button("重新加载当前曲目", ImVec2(160.0f, 32.0f))) {
                pendingSeekMs_ = positionMs_;
                cacheSettingsDialogRequested_ = false;
                ImGui::CloseCurrentPopup();
                startOnlinePlayFor(playingSongId_, curBvid_, curCid_, playingTitle_, playingArtist_);
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("关闭", ImVec2(110.0f, 32.0f))) {
            cacheSettingsDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        (void)previous;
        ImGui::EndPopup();
    }

    // ---- 定时关闭 ----
    if (sleepDialogRequested_) ImGui::OpenPopup("定时关闭");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("定时关闭", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!sleepDialogRequested_) ImGui::CloseCurrentPopup();
        if (sleepTimerActive_) {
            const uint64_t nowTick = GetTickCount64();
            const uint64_t remainingMs =
                sleepTimerEndTick_ > nowTick ? sleepTimerEndTick_ - nowTick : 0;
            ImGui::Text("将在约 %d 分钟后暂停播放", (int)((remainingMs + 59999) / 60000));
        } else {
            ImGui::TextUnformatted("选择多久后暂停播放：");
        }
        ImGui::Spacing();
        const int minuteOptions[] = {15, 30, 60, 90, 120};
        for (int i = 0; i < 5; ++i) {
            const std::string label = std::to_string(minuteOptions[i]) + " 分钟";
            if (ImGui::Button(label.c_str(), ImVec2(120.0f, 32.0f))) {
                startSleepTimer(minuteOptions[i]);
                sleepDialogRequested_ = false;
                ImGui::CloseCurrentPopup();
            }
            if (i % 3 != 2) ImGui::SameLine();
        }
        ImGui::Spacing();
        if (sleepTimerActive_ && ImGui::Button("取消定时", ImVec2(120.0f, 32.0f))) {
            cancelSleepTimer();
        }
        if (sleepTimerActive_) ImGui::SameLine();
        if (ImGui::Button("关闭", ImVec2(120.0f, 32.0f))) {
            sleepDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- 播放队列 ----
    if (queueDialogRequested_) ImGui::OpenPopup("播放队列");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("播放队列", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!queueDialogRequested_) ImGui::CloseCurrentPopup();
        const std::vector<const StoredSong*> queueSongs = currentSongs();
        const StoredPlaylist* queuePlaylist = currentPlaylist();
        ImGui::Text("当前歌单：%s · %d 首",
                    queuePlaylist != nullptr ? Ui::DisplayText(queuePlaylist->title).c_str() : "-",
                    (int)queueSongs.size());
        long long pendingQueueSong = -1;
        ImGui::BeginChild("##queue_list", ImVec2(460.0f, 320.0f), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < queueSongs.size(); ++i) {
            const StoredSong& queueSong = *queueSongs[i];
            const bool isCurrent = queueSong.id == playingSongId_;
            std::string label = std::to_string(i + 1) + ". " + Ui::DisplayText(queueSong.title);
            if (isCurrent) label = "▶ " + label;
            if (ImGui::Selectable(label.c_str(), isCurrent)) pendingQueueSong = queueSong.id;
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (ImGui::Button("关闭", ImVec2(110.0f, 32.0f))) {
            queueDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (pendingQueueSong >= 0) playSongById(pendingQueueSong);
        ImGui::EndPopup();
    }

    // ---- 输出设备 ----
    if (outputDeviceDialogRequested_) ImGui::OpenPopup("输出设备");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("输出设备", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!outputDeviceDialogRequested_) ImGui::CloseCurrentPopup();
        ImGui::TextUnformatted("选择音频输出设备（下一首 / 重新播放后生效）：");
        bool pendingChosen = false;
        std::string pendingDeviceId;
        ImGui::BeginChild("##output_list", ImVec2(460.0f, 240.0f), ImGuiChildFlags_Borders);
        if (ImGui::Selectable("默认设备（系统）", selectedOutputDeviceId_.empty())) {
            pendingChosen = true;
            pendingDeviceId.clear();
        }
        for (const auto& device : outputDevices_) {
            const bool selected = device.first == selectedOutputDeviceId_;
            if (ImGui::Selectable(Ui::DisplayText(device.second).c_str(), selected)) {
                pendingChosen = true;
                pendingDeviceId = device.first;
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (ImGui::Button("刷新", ImVec2(110.0f, 32.0f))) refreshOutputDevices();
        ImGui::SameLine();
        if (ImGui::Button("关闭", ImVec2(110.0f, 32.0f))) {
            outputDeviceDialogRequested_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (pendingChosen) applyOutputDevice(pendingDeviceId);
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
    ImGuiIO& io = ImGui::GetIO();

    // 沉浸模式快捷键（Java 版对齐）：空格 播放/暂停、[ ] 切歌、Esc 退出、F11 全屏切换
    if (immersiveOpen_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) closeImmersive();
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) toggleImmersiveFullscreen();
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) togglePlayback();
        // [ / ] 切歌；← / → 快退/快进 3 秒
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) playPrevious();
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) playNext();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) seekRelative(-3.0);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) seekRelative(3.0);
    }
    // 窗口内快捷键：空格 = 播放/暂停，←/→（或 [ ]）= 切歌，F11 = 全屏
    if (!immersiveOpen_ && ImGui::IsKeyPressed(ImGuiKey_F11, false)) toggleImmersiveFullscreen();
    if (!immersiveOpen_ && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) toggleSidebar();
    if (!immersiveOpen_ && !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) togglePlayback();
        // [ / ] 切歌；← / → 快退/快进 3 秒
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) playPrevious();
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) playNext();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) seekRelative(-3.0);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) seekRelative(3.0);
    }
    const float titleBarHeight = Ui::Dimens::TitleBarHeight;
    const float playerHeight = Ui::Dimens::PlayerHeight;
    const float mainHeight = io.DisplaySize.y - titleBarHeight - playerHeight;
    // 左侧栏折叠/展开的宽度动画（Ctrl+B 或标题栏左侧箭头）
    const float targetSidebarWidth = sidebarCollapsed_ ? 0.0f : (float)Ui::Dimens::SidebarWidth;
    const float sidebarSpeed = 1400.0f * io.DeltaTime;
    if (sidebarAnimWidth_ < targetSidebarWidth) {
        sidebarAnimWidth_ = std::min(targetSidebarWidth, sidebarAnimWidth_ + sidebarSpeed);
    } else {
        sidebarAnimWidth_ = std::max(targetSidebarWidth, sidebarAnimWidth_ - sidebarSpeed);
    }
    const float sidebarWidth = sidebarAnimWidth_;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##root", nullptr, rootFlags);

    if (immersiveOpen_ || immersiveClosing_) {
        drawImmersive();
        ImGui::End();
        return;
    }

    const bool maximized = Ui::WindowIsMaximized(window_);
    ImGui::SetCursorPos(ImVec2(0, 0));
    if (Ui::DrawTitleBar(window_, maximized, sidebarCollapsed_)) toggleSidebar();

    // ---------------- 侧栏 ----------------
    Ui::SidebarActions sidebarActions;
    if (sidebarWidth > 1.0f) {
        ImGui::SetCursorPos(ImVec2(0, titleBarHeight));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Ui::Vec4(Ui::Colors::BgSide));
        ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, mainHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleColor();
        Ui::DrawSidebar(store_, selectedPlaylistId_, mainHeight, status_, statusError_,
                        (int)store_.playlists().size(), accountLoggedIn_, accountName_, accountId_,
                        sidebarActionsExpanded_, &sidebarActions);
        ImGui::EndChild();
    }

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
    playBarData.immersive = immersiveOpen_;
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
    if (sidebarActions.openOutputDevice) {
        refreshOutputDevices();
        outputDeviceDialogRequested_ = true;
    }
    if (sidebarActions.openCacheSettings) cacheSettingsDialogRequested_ = true;
    if (sidebarActions.toggleActionsExpanded) {
        sidebarActionsExpanded_ = !sidebarActionsExpanded_;
        bili_.setSetting("ui.sidebar_actions_expanded", sidebarActionsExpanded_ ? "1" : "0");
    }
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
    if (pageActions.pickCoverFromSong) {
        coverSongDialogRequested_ = true;
        coverSongSelectedId_ = -1;
    }
    if (pageActions.pickCoverFromFile) chooseLocalCover();
    if (pageActions.scanLocalMusic) scanLocalMusic();
    if (pageActions.requestCacheSettings) cacheSettingsDialogRequested_ = true;
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
    if (playBarActions.seekFinished) {
        engine_.seekMs((int64_t)playBarActions.seekMs);
        requestSeekPreview(playBarActions.seekMs);
    }
    if (playBarActions.volumeWheelDelta != 0.0f) {
        // 滚轮微调：每格 1%
        int next = volume_ + (int)std::lround(playBarActions.volumeWheelDelta * 1.0f);
        volume_ = next < 0 ? 0 : (next > 100 ? 100 : next);
        engine_.setVolume(volume_);
        bili_.setSetting("desktop.volume", std::to_string(volume_));
    }
    if (playBarActions.volumeChanged || playBarActions.volumeFinished) {
        volume_ = playBarActions.volume;
        engine_.setVolume(volume_);
        if (playBarActions.volumeFinished) {
            bili_.setSetting("desktop.volume", std::to_string(volume_));
        }
    }
    if (playBarActions.qualitySelected) applyQualitySelection(playBarActions.qualityId);
    if (playBarActions.download) startDownloadCurrent();
    if (playBarActions.openQueue) queueDialogRequested_ = true;
    if (playBarActions.openSleepTimer) sleepDialogRequested_ = true;
    if (playBarActions.openOutputDevice) {
        refreshOutputDevices();
        outputDeviceDialogRequested_ = true;
    }
    if (playBarActions.toggleImmersive) openImmersive();
}
