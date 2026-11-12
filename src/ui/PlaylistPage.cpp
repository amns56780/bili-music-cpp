#include "ui/PlaylistPage.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

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

std::string formatDate(int64_t epochMs) {
    if (epochMs <= 0) return {};
    const std::time_t seconds = (std::time_t)(epochMs / 1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return buffer;
}

std::string formatDuration(int64_t ms) {
    if (ms <= 0) return "0:00";
    const int64_t totalSeconds = ms / 1000;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", (long long)(totalSeconds / 60),
                  (long long)(totalSeconds % 60));
    return buffer;
}

}  // namespace

void DrawPlaylistPage(const LibraryStore& store, const StoredPlaylist* playlist,
                      const std::vector<const StoredSong*>& songs, long long playingSongId,
                      bool selectMode, const std::set<long long>& selectedSongIds,
                      float contentWidth, float contentHeight, PlaylistPageActions* actions,
                      const char* statusText, bool statusIsError, char* searchText,
                      int searchCapacity) {
    const ImVec2 windowPos = ImGui::GetWindowPos();
    auto abs = [&](float x, float y) { return ImVec2(windowPos.x + x, windowPos.y + y); };
    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (playlist == nullptr) {
        draw->AddText(GetFonts().xl, 38.0f, abs(34.0f, 34.0f), Colors::Text3, "还没有选中的歌单");
        draw->AddText(GetFonts().small, 15.0f, abs(34.0f, 80.0f), Colors::Text3,
                      "左侧挑一个歌单；数据来自 Java 版共用的 playlists.json。");
        return;
    }

    const float coverX = Dimens::ContentPaddingX;
    const float coverY = 30.0f;
    CoverImage(playlist->title, abs(coverX, coverY), Dimens::CoverBig, Dimens::RadiusCard,
               playlist->coverUrl, playlist->coverPath);

    const float infoX = coverX + Dimens::CoverBig + Dimens::GapLg;
    const float infoWidth = contentWidth - infoX - Dimens::ContentPaddingX;
    const float coverBottom = coverY + Dimens::CoverBig;

    // 来源标签
    TagChip(sourceLabel(playlist->sourceType), abs(infoX, coverY));
    // 标题
    TextEllipsis(playlist->title, infoWidth - 40.0f, GetFonts().xl, Colors::Text,
                 abs(infoX, coverY + 32.0f));
    // 数量 / 创建日期
    const std::string created = formatDate(playlist->createdAt);
    std::string meta = std::to_string(store.songCount(playlist->id)) + " 首";
    if (!created.empty()) meta += " · " + created + " 创建";
    meta += " · " + sourceLabel(playlist->sourceType);
    draw->AddText(GetFonts().small, 15.0f, abs(infoX, coverY + 78.0f), Colors::Text2, meta.c_str());
    // 标签行
    TagChip("手动新建", abs(infoX, coverY + 104.0f));
    TagChip("B站音源", abs(infoX + 74.0f, coverY + 104.0f));

    // 操作按钮（底对齐到封面底部）
    const float buttonY = coverBottom - 34.0f;
    // 按钮按实际宽度顺排，避免写死坐标后加按钮就挤在一起
    float buttonX = infoX;
    ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
    if (PrimaryButton("play_all", "播放全部", Icons::PlayArrow)) actions->playAll = true;
    buttonX += ImGui::GetItemRectSize().x + 12.0f;
    ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
    if (GhostButton("shuffle_play", "随机播放", Icons::Shuffle)) actions->shufflePlay = true;
    buttonX += ImGui::GetItemRectSize().x + 8.0f;
    ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
    if (GhostButton("select_mode", "多选")) actions->toggleSelectMode = true;
    buttonX += ImGui::GetItemRectSize().x + 4.0f;
    // 歌单内搜索：右对齐在按钮行末尾
    if (searchText != nullptr && searchCapacity > 0) {
        const float searchWidth = 240.0f;
        ImGui::SetCursorPos(ImVec2(infoX + infoWidth - searchWidth, buttonY + 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Colors::TagBg);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##playlist_search", "搜索歌单内歌曲 / UP 主",
                                 searchText, (size_t)searchCapacity);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
    if (IconButton("page_more", Icons::MoreHoriz, 34.0f, 20.0f, Colors::Text2, false, true,
                   "更多操作")) {
        ImGui::OpenPopup("page_menu");
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 8.0f));
    if (ImGui::BeginPopup("page_menu")) {
        if (ImGui::Selectable("重命名歌单")) actions->requestRename = true;
        if (ImGui::Selectable("从歌单视频选封面")) actions->pickCoverFromSong = true;
        if (ImGui::Selectable("从本地图片选封面")) actions->pickCoverFromFile = true;
        if (ImGui::Selectable("扫描本地音乐文件夹")) actions->scanLocalMusic = true;
        if (ImGui::Selectable("分享歌单")) actions->sharePlaylist = true;
        ImGui::Separator();
        if (ImGui::Selectable("删除歌单")) actions->requestDelete = true;
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);

    // ---------------- 多选工具条 ----------------
    const float rowX = Dimens::ContentPaddingX - 12.0f;
    const float rowWidth = contentWidth - rowX * 2.0f;
    const float artistX = rowX + rowWidth - 66.0f - 210.0f;
    float tableTop = coverBottom + 30.0f;
    if (selectMode) {
        const float toolbarY = coverBottom + 12.0f;
        const int selectedCount = (int)selectedSongIds.size();
        const std::string selectedText = "已选 " + std::to_string(selectedCount) + " 首";
        draw->AddText(GetFonts().body, 18.0f, abs(rowX, toolbarY + 6.0f), Colors::Text2,
                      selectedText.c_str());
        ImGui::SetCursorPos(ImVec2(rowX + 120.0f, toolbarY));
        if (GhostButton("sel_all", "全选", 0, 30.0f)) actions->selectAll = true;
        ImGui::SetCursorPos(ImVec2(rowX + 200.0f, toolbarY));
        if (GhostButton("sel_none", "清空", 0, 30.0f)) actions->clearSelection = true;
        ImGui::SetCursorPos(ImVec2(rowX + 280.0f, toolbarY));
        if (GhostButton("sel_copy", "复制到…", 0, 30.0f)) actions->copySelected = true;
        ImGui::SetCursorPos(ImVec2(rowX + 390.0f, toolbarY));
        if (GhostButton("sel_move", "移动到…", 0, 30.0f)) actions->moveSelected = true;
        ImGui::SetCursorPos(ImVec2(rowX + 500.0f, toolbarY));
        if (GhostButton("sel_delete", "删除", 0, 30.0f)) actions->deleteSelected = true;
        ImGui::SetCursorPos(ImVec2(rowX + 600.0f, toolbarY));
        if (GhostButton("sel_done", "完成", 0, 30.0f)) actions->toggleSelectMode = true;
        tableTop = coverBottom + 54.0f;
    }

    // ---------------- 曲目表 ----------------
    draw->AddText(GetFonts().small, 15.0f, abs(rowX + 48.0f - 8.0f, tableTop + 9.0f), Colors::Text3,
                  selectMode ? "选择" : "#");
    draw->AddText(GetFonts().small, 15.0f, abs(rowX + 48.0f + 24.0f, tableTop + 9.0f), Colors::Text3, "音乐标题");
    draw->AddText(GetFonts().small, 15.0f, abs(artistX, tableTop + 9.0f), Colors::Text3, "UP主");
    const float durationHeaderWidth = MeasureText(GetFonts().small, 15.0f, "时长");
    // 表头与数值同样右对齐到 rowWidth - 30（子窗口 padding 为 0，行的 pos.x 就是 rowX）
    draw->AddText(GetFonts().small, 15.0f,
                  abs(rowX + rowWidth - 30.0f - durationHeaderWidth, tableTop + 9.0f), Colors::Text3,
                  "时长");
    DividerLine(rowX, rowX + rowWidth, tableTop + Dimens::TableHeaderHeight);

    const float rowsTop = tableTop + Dimens::TableHeaderHeight + 1.0f;
    ImGui::SetCursorPos(ImVec2(rowX, rowsTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##song_rows", ImVec2(rowWidth, contentHeight - rowsTop - 8.0f), ImGuiChildFlags_None);

    // 拖动排序 / 框选多选的状态（按歌单隔离，切歌单自动复位）
    static long long dragScopePlaylistId = -1;
    static long long dragSongId = -1;
    static bool dragSongActive = false;
    static bool bandArmed = false;
    static bool bandActive = false;
    static ImVec2 bandStart(0.0f, 0.0f);
    if (dragScopePlaylistId != playlist->id) {
        dragScopePlaylistId = playlist->id;
        dragSongId = -1;
        dragSongActive = false;
        bandArmed = false;
        bandActive = false;
    }

    struct RowHit {
        long long id = 0;
        ImVec2 min;
        ImVec2 max;
    };
    std::vector<RowHit> rowHits;
    rowHits.reserve(songs.size());
    long long pendingToggleId = -1;

    // 行内容必须画在这个子窗口自己的 draw list 上：用外层窗口的 draw list 画，滚出视区的行不会被
    // 子窗口裁剪，会漏到表头 / 封面上（表现为「时长」列一串时间飘在表头上面）。
    ImDrawList* rowDraw = ImGui::GetWindowDrawList();
    if (songs.empty()) {
        ImGui::SetCursorPos(ImVec2(20.0f, 30.0f));
        TextBodySm("这个歌单还没有曲目。");
    }
    for (size_t i = 0; i < songs.size(); ++i) {
        const StoredSong& song = *songs[i];
        ImGui::PushID((int)song.id);
        ImGui::InvisibleButton("##song", ImVec2(rowWidth, Dimens::RowHeight));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 pos = ImGui::GetItemRectMin();
        const bool isPlaying = song.id == playingSongId;
        if (isPlaying) {
            rowDraw->AddRectFilled(pos, ImVec2(pos.x + rowWidth, pos.y + Dimens::RowHeight),
                                   Colors::BrandSoft, Dimens::RadiusRow);
        } else if (hovered) {
            rowDraw->AddRectFilled(pos, ImVec2(pos.x + rowWidth, pos.y + Dimens::RowHeight),
                                   Colors::Hover, Dimens::RadiusRow);
        }
        // 多选模式：勾选框；普通模式：序号 / 播放中图标
        if (selectMode) {
            const bool checked = selectedSongIds.count(song.id) != 0;
            const ImVec2 box(pos.x + 22.0f, pos.y + (Dimens::RowHeight - 18.0f) * 0.5f);
            rowDraw->AddRectFilled(box, ImVec2(box.x + 18.0f, box.y + 18.0f),
                                   checked ? Colors::Brand : Colors::TagBg, 4.0f);
            rowDraw->AddRect(ImVec2(box.x + 0.5f, box.y + 0.5f),
                             ImVec2(box.x + 17.5f, box.y + 17.5f),
                             checked ? Colors::Brand : Colors::Text3, 4.0f);
            if (checked) {
                const ImU32 white = Colors::White;
                rowDraw->AddLine(ImVec2(box.x + 4.0f, box.y + 9.0f), ImVec2(box.x + 8.0f, box.y + 13.0f),
                                 white, 2.0f);
                rowDraw->AddLine(ImVec2(box.x + 8.0f, box.y + 13.0f), ImVec2(box.x + 14.0f, box.y + 5.0f),
                                 white, 2.0f);
            }
        } else if (isPlaying) {
            const std::string note = Icons::Utf8(Icons::GraphicEq);
            rowDraw->AddText(GetFonts().small, 15.0f, ImVec2(pos.x + 26.0f, pos.y + 14.0f),
                             Colors::Brand, note.c_str());
        } else {
            char index[16];
            std::snprintf(index, sizeof(index), "%d", (int)(i + 1));
            const float indexWidth = MeasureText(GetFonts().small, 15.0f, index);
            rowDraw->AddText(GetFonts().small, 15.0f, ImVec2(pos.x + 48.0f - indexWidth, pos.y + 15.0f),
                             Colors::Text3, index);
        }
        // 标题
        TextEllipsis(song.title, artistX - (pos.x + 48.0f + 24.0f) - 16.0f, GetFonts().body,
                     isPlaying ? Colors::Brand : Colors::Text, ImVec2(pos.x + 48.0f + 24.0f, pos.y + 11.0f));
        // UP主
        TextEllipsis(song.artist, 230.0f, GetFonts().small, Colors::Text2, ImVec2(artistX, pos.y + 12.0f));
        // 时长
        const std::string duration = formatDuration(song.durationMs);
        const float durationWidth = MeasureText(GetFonts().small, 15.0f, duration);
        rowDraw->AddText(GetFonts().small, 15.0f,
                         ImVec2(pos.x + rowWidth - 30.0f - durationWidth, pos.y + 15.0f), Colors::Text3,
                         duration.c_str());
        rowHits.push_back({song.id, pos, ImVec2(pos.x + rowWidth, pos.y + Dimens::RowHeight)});
        // 普通模式：按住某一行上下拖动 = 调整歌单内顺序
        if (!selectMode && ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f)) {
            dragSongActive = true;
            dragSongId = song.id;
        }
        if (selectMode) {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) pendingToggleId = song.id;
        } else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !dragSongActive) {
            actions->playSongId = song.id;
        }
        ImGui::PopID();
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool hoveringRows = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // 拖动时靠近上下边缘自动滚动（长歌单也能拖到屏幕外）
    auto autoScrollNearEdge = [&]() {
        const float winTop = ImGui::GetWindowPos().y;
        const float winBottom = winTop + ImGui::GetWindowSize().y;
        const float edge = 44.0f;
        float step = 0.0f;
        if (mouse.y < winTop + edge) {
            step = -12.0f * ((winTop + edge - mouse.y) / edge);
        } else if (mouse.y > winBottom - edge) {
            step = 12.0f * ((mouse.y - (winBottom - edge)) / edge);
        }
        if (step != 0.0f) ImGui::SetScrollY(ImGui::GetScrollY() + step);
    };

    // ---------- 普通模式：拖动排序，落点画一条红线 ----------
    int dragTargetIndex = -1;
    if (dragSongActive) {
        // 目标 = 「插入到第几行之前」，默认追加到末尾（rowHits.size()）
        dragTargetIndex = (int)rowHits.size();
        for (size_t i = 0; i < rowHits.size(); ++i) {
            if (mouse.y < (rowHits[i].min.y + rowHits[i].max.y) * 0.5f) {
                dragTargetIndex = (int)i;
                break;
            }
        }
        if (dragTargetIndex < 0) dragTargetIndex = 0;
        float lineY = rowHits.empty() ? rowsTop : rowHits.back().max.y;
        if (dragTargetIndex < (int)rowHits.size()) lineY = rowHits[(size_t)dragTargetIndex].min.y;
        const float lineX0 = rowHits.empty() ? rowX : rowHits[0].min.x;
        const float lineX1 = rowHits.empty() ? rowX + rowWidth : rowHits[0].max.x;
        rowDraw->AddLine(ImVec2(lineX0, lineY), ImVec2(lineX1, lineY), Colors::Brand, 2.0f);
        // 被拖起的行变亮 + 上下拖动光标：给「抓住了」的确认感
        for (const RowHit& hit : rowHits) {
            if (hit.id == dragSongId) {
                rowDraw->AddRectFilled(hit.min, hit.max, IM_COL32(255, 255, 255, 0x1E),
                                       Dimens::RadiusRow);
                rowDraw->AddRect(hit.min, hit.max, IM_COL32(255, 255, 255, 0x38), Dimens::RadiusRow);
                break;
            }
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        autoScrollNearEdge();
        if (mouseReleased) {
            actions->reorderSong = true;
            actions->reorderSongId = dragSongId;
            actions->reorderSongIndex = dragTargetIndex;
            dragSongActive = false;
            dragSongId = -1;
        }
    }

    // ---------- 多选模式：按住拖动框选（类似资源管理器） ----------
    if (selectMode) {
        if (hoveringRows && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            bandStart = mouse;
            bandArmed = true;
            bandActive = false;
        }
        if (bandArmed && mouseDown) {
            if (!bandActive && (std::fabs(mouse.x - bandStart.x) > 4.0f ||
                                std::fabs(mouse.y - bandStart.y) > 4.0f)) {
                bandActive = true;
            }
            if (bandActive) {
                const ImVec2 bandMin(std::min(bandStart.x, mouse.x), std::min(bandStart.y, mouse.y));
                const ImVec2 bandMax(std::max(bandStart.x, mouse.x), std::max(bandStart.y, mouse.y));
                rowDraw->AddRectFilled(bandMin, bandMax, IM_COL32(0xEC, 0x41, 0x41, 0x30), 2.0f);
                rowDraw->AddRect(bandMin, bandMax, IM_COL32(0xEC, 0x41, 0x41, 0xCC), 2.0f);
                autoScrollNearEdge();
            }
        }
        if (bandArmed && mouseReleased) {
            if (bandActive) {
                const ImVec2 bandMin(std::min(bandStart.x, mouse.x), std::min(bandStart.y, mouse.y));
                const ImVec2 bandMax(std::max(bandStart.x, mouse.x), std::max(bandStart.y, mouse.y));
                std::vector<long long> hits;
                for (const RowHit& hit : rowHits) {
                    if (hit.max.x >= bandMin.x && hit.min.x <= bandMax.x && hit.max.y >= bandMin.y &&
                        hit.min.y <= bandMax.y) {
                        hits.push_back(hit.id);
                    }
                }
                actions->setSelection = true;
                actions->selectionIds = std::move(hits);
            } else if (pendingToggleId >= 0) {
                actions->toggleSong = true;
                actions->toggleSongId = pendingToggleId;
            }
            bandArmed = false;
            bandActive = false;
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (statusText != nullptr && statusText[0] != '\0') {
        const ImU32 color = statusIsError ? Colors::Brand : Colors::Text3;
        TextEllipsis(statusText, contentWidth - 80.0f, GetFonts().small, color,
                     abs(Dimens::ContentPaddingX, contentHeight - 26.0f));
    }
}

}  // namespace Ui
