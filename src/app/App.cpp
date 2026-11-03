#include "app/App.h"

#include <cmath>
#include <cstdlib>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <GLFW/glfw3.h>

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
    stallWatchStartTick_ = GetTickCount64();
    backupIndex_ = 0;
    setStatus("正在取流：" + title + "（" + bvid + "，音质 " + qualityNameForId(qualityId_) + "）");
    const int qualityIdSnapshot = qualityId_;
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

void App::checkPlaybackStall() {
    if (!currentIsOnline_) return;
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
        if (event.kind == AsyncEvent::Kind::Play && event.songId != pendingSongId_) continue;
        if (!event.ok) {
            if (event.kind == AsyncEvent::Kind::Play) pendingSongId_ = -1;
            setStatus("取流失败：" + event.message, true);
            continue;
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

    if (ImGui::BeginPopupModal("登录 B 站", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("浏览器登录 bilibili.com 后，把整段 Cookie 粘贴到这里：");
        ImGui::TextDisabled("格式：SESSDATA=xxx; bili_jct=yyy; DedeUserID=123; ...");
        ImGui::InputTextMultiline("##cookie", cookieInput_, sizeof(cookieInput_), ImVec2(560.0f, 200.0f));
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
        ImGui::EndPopup();
    }
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
    Ui::DrawPlaylistPage(store_, currentPlaylist(), currentSongs(), playingSongId_,
                         io.DisplaySize.x - sidebarWidth, mainHeight, &pageActions, nullptr, false);
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
    if (sidebarActions.createPlaylist) setStatus("新建歌单：M3 实现（当前数据由 Java 版维护）");
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
        for (const StoredPlaylist& playlist : store_.playlists()) {
            if (playlist.title == "本地音乐") {
                selectPlaylist(playlist.id);
                break;
            }
        }
    }
    if (sidebarActions.toggleImportMenu) {
        importMenuOpen_ = !importMenuOpen_;
        setStatus(importMenuOpen_ ? "导入歌单：M3 实现（收藏夹/稍后再看/UP主投稿/合集）" : "就绪");
    }

    if (pageActions.playAll) playFirstPlayable();
    if (pageActions.playSongId >= 0) playSongById(pageActions.playSongId);
    if (pageActions.selectMode) setStatus("多选 / 复制 / 移动：M3 实现");
    if (pageActions.openMenu) setStatus("歌单菜单（重命名 / 封面 / 分享 / 删除）：M3 实现");

    if (playBarActions.togglePlay) {
        if (hasMedia_) {
            engine_.togglePause();
        } else {
            playFirstPlayable();
        }
    }
    if (playBarActions.previous) stepSong(-1);
    if (playBarActions.next) stepSong(1);
    if (playBarActions.cycleMode) {
        mode_ = (mode_ + 1) % 4;
        setStatus("播放模式：" + formatModeName(mode_));
    }
    if (playBarActions.seekFinished) engine_.seekMs((int64_t)playBarActions.seekMs);
    if (playBarActions.volumeChanged || playBarActions.volumeFinished) {
        volume_ = playBarActions.volume;
        engine_.setVolume(volume_);
    }
    if (playBarActions.qualitySelected) applyQualitySelection(playBarActions.qualityId);
    if (playBarActions.download) setStatus("下载当前曲目：M3 实现");
    if (playBarActions.openQueue) setStatus("播放队列：M4 实现");
    if (playBarActions.openSleepTimer) setStatus("定时关闭：M4 实现");
    if (playBarActions.openOutputDevice) setStatus("输出设备：M4 实现");
}
