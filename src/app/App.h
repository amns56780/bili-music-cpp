#pragma once

#include <cstdint>
#include <string>

#include "data/LibraryStore.h"
#include "media/VlcEngine.h"

// C++ 版桌面端的应用层：一个精简的「歌单 + 播放」界面。
//
// M1 目标：读 Java 版留下的 playlists.json，列出歌单和本地音乐，双击用内置 VLC 播放本地文件，
// 底部播放条支持 暂停/停止/进度/音量。在线曲目（需要 WBI 取流）留给 M2。
class App {
public:
    void init();
    void draw();
    void shutdown();

private:
    void setupFonts();
    void setupStyle();

    void drawSidebar();
    void drawSongList();
    void drawPlayBar();
    void playSong(const StoredSong& song);
    void openLocalFile();

    void setStatus(const std::string& text, bool isError = false);
    const StoredPlaylist* selectedPlaylist() const;
    int64_t currentPositionMs() const;
    int64_t currentLengthMs() const;

    LibraryStore store_;
    VlcEngine engine_;
    std::string status_ = "就绪";
    bool statusError_ = false;

    int64_t selectedPlaylistId_ = -1;
    int64_t playingSongId_ = -1;
    std::string nowPlayingTitle_;
    int volume_ = 80;
    int64_t seekPreviewMs_ = -1;
};
