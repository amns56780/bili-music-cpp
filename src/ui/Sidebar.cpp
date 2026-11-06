#include "ui/Sidebar.h"

#include <string>

#include "ui/Components.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace Ui {

namespace {

std::string sourceLabel(const std::string& sourceType) {
    if (sourceType == "FAV") return "收藏夹";
    if (sourceType == "TOVIEW" || sourceType == "TO_VIEW") return "稍后再看";
    if (sourceType == "UPLOAD" || sourceType == "SPACE") return "UP主投稿";
    if (sourceType == "SEASON") return "合集";
    if (sourceType == "LOCAL") return "本地";
    return "手动新建";
}

void drawPlaylistRow(const ImVec2& origin, const StoredPlaylist& playlist, const std::string& subtitle,
                     bool selected, float rowWidth, SidebarActions* actions) {
    ImGui::PushID((int)playlist.id);
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX(), ImGui::GetCursorPosY()));
    const bool pressed = ImGui::InvisibleButton("##row", ImVec2(rowWidth, 54.0f));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 pos = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (selected || hovered) {
        draw->AddRectFilled(pos, ImVec2(pos.x + rowWidth, pos.y + 54.0f),
                            selected ? Colors::Active : Colors::Hover, Dimens::RadiusRow);
    }
    if (selected) {
        draw->AddRectFilled(ImVec2(pos.x + 3.0f, pos.y + 27.0f - 12.0f),
                            ImVec2(pos.x + 6.0f, pos.y + 27.0f + 12.0f), Colors::Brand, 2.0f);
    }
    CoverImage(playlist.title.empty() ? playlist.sourceType : playlist.title,
               ImVec2(pos.x + 11.0f, pos.y + (54.0f - Dimens::CoverSide) / 2.0f),
               Dimens::CoverSide, Dimens::RadiusCoverSm, playlist.coverUrl, playlist.coverPath);
    const float textX = pos.x + 11.0f + Dimens::CoverSide + 10.0f;
    const float textWidth = rowWidth - (textX - pos.x) - 10.0f;
    TextEllipsis(playlist.title, textWidth, GetFonts().body, Colors::Text,
                 ImVec2(textX, pos.y + 10.0f));
    TextEllipsis(subtitle, textWidth, GetFonts().small, Colors::Text3,
                 ImVec2(textX, pos.y + 30.0f));
    ImGui::PopID();
    if (pressed) {
        actions->selectPlaylist = true;
        actions->playlistId = playlist.id;
    }
}

}  // namespace

void DrawSidebar(const LibraryStore& store, long long selectedPlaylistId, float height,
                 const std::string& statusText, bool statusIsError, int playlistCount,
                 bool loggedIn, const std::string& userName, long long userId,
                 bool actionsExpanded, SidebarActions* actions) {
    const float width = Dimens::SidebarWidth;

    // 本函数接收到的工作区（子窗口）左上角，全部局部坐标都基于它换算
    const ImVec2 windowPos = ImGui::GetWindowPos();
    auto abs = [&](float x, float y) { return ImVec2(windowPos.x + x, windowPos.y + y); };
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // ---------------- 账号卡 ----------------
    const std::string avatarSeed = loggedIn && !userName.empty() ? userName : "B";
    CoverPlaceholder(avatarSeed, abs(16.0f, 18.0f), 36.0f, 18.0f);
    const std::string displayName = loggedIn ? userName : "未登录";
    TextEllipsis(displayName, 120.0f, GetFonts().body, Colors::Text, abs(62.0f, 21.0f));
    const std::string accountSub = loggedIn ? ("UID " + std::to_string(userId)) : "本地播放可用";
    TextEllipsis(accountSub, 120.0f, GetFonts().small, Colors::Text3, abs(62.0f, 40.0f));
    ImGui::SetCursorPos(ImVec2(width - 10.0f - 64.0f, 20.0f));
    if (GhostButton("##login", loggedIn ? "退出" : "登录", 0, 28.0f)) {
        if (loggedIn) {
            actions->logout = true;
        } else {
            actions->login = true;
        }
    }

    // ---------------- 粘贴链接入口 ----------------
    ImGui::SetCursorPos(ImVec2(12.0f, 66.0f));
    ImGui::PushID("paste_link");
    const bool linkPressed = ImGui::InvisibleButton("##link", ImVec2(width - 24.0f, 32.0f));
    const bool linkHovered = ImGui::IsItemHovered();
    if (linkHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 linkPos = ImGui::GetItemRectMin();
    draw->AddRectFilled(linkPos, ImVec2(linkPos.x + width - 24.0f, linkPos.y + 32.0f), Colors::Hover, 16.0f);
    const std::string linkIcon = Icons::Utf8(Icons::Link);
    // 按墨迹实测上移：图标 +7、文字 +7.5，上下留白一致
    draw->AddText(GetFonts().body, 18.0f, ImVec2(linkPos.x + 12.0f, linkPos.y + 7.0f), Colors::Text3,
                  linkIcon.c_str());
    draw->AddText(GetFonts().small, 15.0f,
                  ImVec2(linkPos.x + 12.0f + 16.0f + 8.0f, linkPos.y + 7.5f), Colors::Text3,
                  "粘贴链接 / BV 号 / 歌单分享码");
    ImGui::PopID();
    if (linkPressed) actions->pasteLink = true;

    // ---------------- 分组标题 ----------------
    draw->AddText(GetFonts().small, 15.0f, abs(18.0f, 116.0f), Colors::Text3, "我的歌单");
    ImGui::SetCursorPos(ImVec2(width - 10.0f - 28.0f, 106.0f));
    if (IconButton("##new_playlist", Icons::Add, 28.0f, 16.0f, Colors::Text2, false, true,
                   "新建歌单（M3 实现）")) {
        actions->createPlaylist = true;
    }

    // ---------------- 歌单列表 ----------------
    const float listTop = 146.0f;
    // 底部区域：折叠标题行 +（展开时的 5 个入口）
    const float actionsHeaderHeight = 30.0f;
    const float actionRowHeight = 36.0f;
    const float bottomArea = 8.0f + actionsHeaderHeight +
                             (actionsExpanded ? 5.0f * actionRowHeight : 0.0f) + 6.0f;
    const float statusArea = statusText.empty() ? 0.0f : 22.0f;
    const float listHeight = height - listTop - bottomArea - statusArea;
    if (listHeight > 40.0f) {
        ImGui::SetCursorPos(ImVec2(8.0f, listTop));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##playlist_list", ImVec2(width - 16.0f, listHeight), ImGuiChildFlags_None);
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        for (const StoredPlaylist& playlist : store.playlists()) {
            const std::string subtitle = sourceLabel(playlist.sourceType) + " · " +
                                         std::to_string(store.songCount(playlist.id)) + " 首";
            drawPlaylistRow(ImGui::GetWindowPos(), playlist, subtitle,
                            playlist.id == selectedPlaylistId, rowWidth, actions);
            ImGui::Dummy(ImVec2(1.0f, 2.0f));
        }
        if (store.playlists().empty()) {
            ImGui::SetCursorPos(ImVec2(12.0f, 8.0f));
            TextLabel("还没有歌单。先用 Java 版导入歌单（数据目录共用）。");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---------------- 状态行 + 分隔线 + 底部入口 ----------------
    const float dividerY = height - bottomArea;
    if (!statusText.empty()) {
        TextEllipsis(statusText, width - 24.0f, GetFonts().small,
                     statusIsError ? Colors::Brand : Colors::Text3, abs(12.0f, dividerY - 22.0f));
        // 状态文字被截断时，悬停显示完整内容
        ImGui::SetCursorPos(ImVec2(8.0f, dividerY - 24.0f));
        ImGui::PushID("status_hint");
        ImGui::InvisibleButton("##status_hint", ImVec2(width - 16.0f, 20.0f));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", statusText.c_str());
        ImGui::PopID();
    }
    DividerLine(0.0f, width, dividerY);

    // 折叠/展开底部入口的标题行
    float actionY = dividerY + 6.0f;
    ImGui::SetCursorPos(ImVec2(8.0f, actionY));
    ImGui::PushID("actions_header");
    const float actionsHeaderWidth = width - 16.0f;
    const bool actionsHeaderPressed =
        ImGui::InvisibleButton("##actions_header", ImVec2(actionsHeaderWidth, actionsHeaderHeight));
    const bool actionsHeaderHovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (actionsHeaderHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddRectFilled(ImGui::GetItemRectMin(),
                            ImVec2(ImGui::GetItemRectMin().x + actionsHeaderWidth,
                                   ImGui::GetItemRectMin().y + actionsHeaderHeight),
                            Colors::Hover, 8.0f);
    }
    const ImVec2 actionsHeaderPos = ImGui::GetItemRectMin();
    draw->AddText(GetFonts().small, 15.0f,
                  ImVec2(actionsHeaderPos.x + 10.0f,
                         actionsHeaderPos.y + (actionsHeaderHeight - 15.0f) * 0.5f + 1.0f),
                  Colors::Text3, "功能");
    // 展开时箭头朝下（指向下方内容），收起时朝上（表示点击展开）
    const std::string chevron =
        Icons::Utf8(actionsExpanded ? Icons::ExpandMore : Icons::ExpandLess);
    draw->AddText(GetFonts().body, 18.0f,
                  ImVec2(actionsHeaderPos.x + actionsHeaderWidth - 28.0f,
                         actionsHeaderPos.y + (actionsHeaderHeight - 18.0f) * 0.5f + 2.5f),
                  actionsHeaderHovered ? Colors::Text2 : Colors::Text3, chevron.c_str());
    if (actionsHeaderPressed) actions->toggleActionsExpanded = true;

    if (actionsExpanded) {
        actionY += actionsHeaderHeight + 2.0f;
        ImGui::SetCursorPos(ImVec2(8.0f, actionY));
        if (SidebarAction("local_music", Icons::FolderOpen, "本地音乐", 36.0f,
                          "扫描本地音乐文件夹：点右上角「…」菜单 →「扫描本地音乐文件夹」")) {
            actions->openLocalMusic = true;
        }
        actionY += actionRowHeight;
        ImGui::SetCursorPos(ImVec2(8.0f, actionY));
        if (SidebarAction("output_device", Icons::Headset, "输出设备", 36.0f,
                          "选择音乐输出到哪个设备；下一首 / 重新播放后生效")) {
            actions->openOutputDevice = true;
        }
        actionY += actionRowHeight;
        ImGui::SetCursorPos(ImVec2(8.0f, actionY));
        if (SidebarAction("cache_settings", Icons::Tune, "播放缓存", 36.0f,
                          "网络缓存范围、整首缓存开关、清除播放缓存")) {
            actions->openCacheSettings = true;
        }
        actionY += actionRowHeight;
        ImGui::SetCursorPos(ImVec2(8.0f, actionY));
        if (SidebarAction("import_playlist", Icons::CloudDownload,
                          "导入歌单（" + std::to_string(playlistCount) + "）", 36.0f,
                          "从 B 站收藏夹 / UP 主投稿 / 合集 / 视频链接导入歌单")) {
            actions->toggleImportMenu = true;
        }
        actionY += actionRowHeight;
        ImGui::SetCursorPos(ImVec2(8.0f, actionY));
        if (SidebarAction("app_settings", Icons::Settings, "设置", 36.0f,
                          "版本信息、数据目录、播放缓存、卸载")) {
            actions->openSettings = true;
        }
    }
}

}  // namespace Ui
