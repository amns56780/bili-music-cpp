#pragma once

#include <cstdint>

#include "data/LibraryStore.h"

namespace Ui {

// 侧栏（240px）：账号卡、粘贴链接、我的歌单、底部入口。
// 对应 Java 版 Sidebar.kt；点击行为通过 [SidebarActions] 回传给 App。
struct SidebarActions {
    bool selectPlaylist = false;
    long long playlistId = -1;
    bool createPlaylist = false;
    bool openLocalMusic = false;
    bool openOutputDevice = false;
    bool openCacheSettings = false;
    bool openSettings = false;
    bool toggleImportMenu = false;
    bool pasteLink = false;
    bool login = false;
    bool logout = false;
    bool toggleActionsExpanded = false;
    // 拖动排序歌单
    bool reorderPlaylist = false;
    long long reorderPlaylistId = -1;
    int reorderPlaylistIndex = -1;
};

void DrawSidebar(const LibraryStore& store, long long selectedPlaylistId, float height,
                 const std::string& statusText, bool statusIsError, int playlistCount,
                 bool loggedIn, const std::string& userName, long long userId,
                 bool actionsExpanded, SidebarActions* actions);

}  // namespace Ui
