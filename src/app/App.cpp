#include "app/App.h"

#include <cmath>
#include <cstdlib>

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

namespace {

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size);
    return out;
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

    std::string engineError;
    if (!engine_.init(&engineError)) {
        setStatus("VLC 内核加载失败：" + engineError, true);
    }

    std::string loadError;
    if (!store_.load(&loadError)) {
        setStatus(loadError, true);
        return;
    }
    if (!store_.playlists().empty()) {
        selectPlaylist(store_.playlists().front().id);
    }
    if (engine_.ready()) {
        setStatus("VLC " + engine_.version());
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

void App::playSongById(long long songId) {
    const StoredSong* song = findSong(songId);
    if (song == nullptr) return;
    playingSongId_ = song->id;
    playingTitle_ = song->title;
    playingArtist_ = song->artist;
    if (song->localPath.empty()) {
        setStatus("在线曲目「" + song->title + "」需要 M2 的 WBI 取流（当前先跑通 UI + 本地播放）", false);
        return;
    }
    std::string error;
    if (engine_.playPath(utf8ToWide(song->localPath), &error)) {
        engine_.setVolume(volume_);
        setStatus("正在播放（本地文件）：" + song->title);
    } else {
        setStatus("播放失败：" + error, true);
    }
}

void App::playFirstPlayable() {
    for (const StoredSong* song : currentSongs()) {
        if (!song->localPath.empty()) {
            playSongById(song->id);
            return;
        }
    }
    setStatus("这个歌单没有本地曲目：在线播放要等 M2 的取流实现", false);
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
        if (!songs[(size_t)index]->localPath.empty()) {
            playSongById(songs[(size_t)index]->id);
            return;
        }
        if (mode_ == 2) break;
    }
    setStatus("附近没有可播放的本地曲目（在线曲目需要 M2）", false);
}

void App::syncPlaybackState() {
    hasMedia_ = engine_.ready() && engine_.lengthMs() > 0;
    positionMs_ = engine_.ready() ? (double)engine_.positionMs() : 0.0;
    durationMs_ = engine_.ready() ? (double)engine_.lengthMs() : 0.0;
    playing_ = engine_.ready() && engine_.isPlaying();
    if (hasMedia_ && engine_.volume() > 0) volume_ = engine_.volume();
}

void App::draw() {
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

    const bool maximized = window_ != nullptr && glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0;
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
                    (int)store_.playlists().size(), &sidebarActions);
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
    Ui::PlayBarActions playBarActions;
    Ui::DrawPlayBar(playBarData, &playBarActions, io.DisplaySize.x, playerHeight);
    ImGui::EndChild();

    ImGui::End();

    // ================= 动作分发 =================  
    if (sidebarActions.selectPlaylist) {
        selectPlaylist(sidebarActions.playlistId);
    }
    if (sidebarActions.createPlaylist) {
        setStatus("新建歌单：M3 实现（当前数据由 Java 版维护）", false);
    }
    if (sidebarActions.pasteLink) {
        setStatus("粘贴链接播放：M2 实现（需要 WBI 取流）", false);
    }
    if (sidebarActions.login) {
        setStatus("扫码 / Cookie 登录：M2 实现", false);
    }
    if (sidebarActions.openOutputDevice) {
        setStatus("输出设备选择：M4 实现（libVLC audio output device 接口）", false);
    }
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
        setStatus(importMenuOpen_ ? "导入歌单：M3 实现（收藏夹/稍后再看/UP主投稿/合集）" : "就绪", false);
    }

    if (pageActions.playAll) playFirstPlayable();
    if (pageActions.playSongId >= 0) playSongById(pageActions.playSongId);
    if (pageActions.selectMode) setStatus("多选 / 复制 / 移动：M3 实现", false);
    if (pageActions.openMenu) setStatus("歌单菜单（重命名 / 封面 / 分享 / 删除）：M3 实现", false);

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
        setStatus("播放模式：" + formatModeName(mode_), false);
    }
    if (playBarActions.seekFinished) engine_.seekMs((int64_t)playBarActions.seekMs);
    if (playBarActions.volumeFinished || playBarActions.volume != volume_) {
        volume_ = playBarActions.volume;
        engine_.setVolume(volume_);
    }
    if (playBarActions.download) setStatus("下载当前曲目：M3 实现", false);
    if (playBarActions.openQueue) setStatus("播放队列：M4 实现", false);
    if (playBarActions.openSleepTimer) setStatus("定时关闭：M4 实现", false);
    if (playBarActions.openOutputDevice) setStatus("输出设备：M4 实现", false);
}
