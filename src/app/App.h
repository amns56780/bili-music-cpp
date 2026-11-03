#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/LibraryStore.h"
#include "media/VlcEngine.h"

struct GLFWwindow;

// C++ 版桌面端的应用层（M1.5）：按 Java 版布局把 UI 拆成
// 标题栏 / 侧栏 / 歌单页 / 播放条四个模块，这里只负责状态与动作分发。
class App {
public:
    void init(GLFWwindow* window);
    void draw();
    void shutdown();

private:
    void syncPlaybackState();
    void selectPlaylist(long long playlistId);
    void playSongById(long long songId);
    void playFirstPlayable();
    void stepSong(int direction);
    void setStatus(const std::string& text, bool isError = false);
    const StoredPlaylist* currentPlaylist() const;
    std::vector<const StoredSong*> currentSongs() const;
    const StoredSong* findSong(long long songId) const;

    GLFWwindow* window_ = nullptr;
    LibraryStore store_;
    VlcEngine engine_;

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
};
