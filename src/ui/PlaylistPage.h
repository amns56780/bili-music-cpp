#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "data/LibraryStore.h"

namespace Ui {

// 「歌单页」：大封面 + 歌单信息 + 操作按钮 + 曲目表（对应 Java 版 PlaylistPage.kt）。
// M1.5 阶段只实现视觉/交互骨架：播放全部对本地曲目生效，重命名/删除等菜单项等 M3。
struct PlaylistPageActions {
    bool playAll = false;
    bool selectMode = false;
    bool openMenu = false;
    bool requestRename = false;
    bool requestDelete = false;
    bool pickCoverFromSong = false;
    bool pickCoverFromFile = false;
    bool scanLocalMusic = false;

    // 多选
    bool toggleSelectMode = false;
    bool toggleSong = false;
    long long toggleSongId = -1;
    bool selectAll = false;
    bool clearSelection = false;
    bool copySelected = false;
    bool moveSelected = false;
    bool deleteSelected = false;
    long long playSongId = -1;
    bool showMenuAboutEdit = false;
};

void DrawPlaylistPage(const LibraryStore& store, const StoredPlaylist* playlist,
                      const std::vector<const StoredSong*>& songs, long long playingSongId,
                      bool selectMode, const std::set<long long>& selectedSongIds,
                      float contentWidth, float contentHeight, PlaylistPageActions* actions,
                      const char* statusText, bool statusIsError);

}  // namespace Ui
