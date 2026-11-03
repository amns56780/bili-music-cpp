#include "app/App.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

#include <cstdio>
#include <cstring>

#include "imgui.h"

namespace {

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size);
    return out;
}

std::string formatDuration(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t totalSeconds = ms / 1000;
    int64_t hours = totalSeconds / 3600;
    int64_t minutes = (totalSeconds % 3600) / 60;
    int64_t seconds = totalSeconds % 60;
    char buffer[32];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld",
                      (long long)hours, (long long)minutes, (long long)seconds);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld", (long long)minutes, (long long)seconds);
    }
    return buffer;
}

const char* sourceLabel(const std::string& sourceType) {
    if (sourceType == "FAV") return "收藏夹";
    if (sourceType == "TOVIEW" || sourceType == "TO_VIEW") return "稍后再看";
    if (sourceType == "UPLOAD" || sourceType == "SPACE") return "UP主投稿";
    if (sourceType == "SEASON") return "合集";
    if (sourceType == "LOCAL") return "本地";
    return "手动新建";
}

bool pickAudioFile(std::wstring& outPath) {
    wchar_t buffer[32768] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"音频文件\0*.mp3;*.m4a;*.flac;*.wav;*.aac;*.ogg;*.opus;*.mp4\0所有文件\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = (DWORD)(sizeof(buffer) / sizeof(wchar_t));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;
    outPath = buffer;
    return true;
}

}  // namespace

void App::init() {
    setupStyle();
    setupFonts();

    std::string error;
    if (!engine_.init(&error)) {
        setStatus("VLC 内核加载失败：" + error, true);
    } else {
        setStatus("VLC " + engine_.version() + " · " + wideToUtf8(engine_.libraryDir()), false);
    }

    std::string loadError;
    if (!store_.load(&loadError)) {
        setStatus(loadError, true);
        return;
    }
    if (!store_.playlists().empty()) {
        selectedPlaylistId_ = store_.playlists().front().id;
    }
}

void App::shutdown() {
    engine_.shutdown();
}

void App::setupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;

    auto color = [](int hex, float alpha = 1.0f) {
        return ImVec4(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                      (hex & 0xFF) / 255.0f, alpha);
    };

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = color(0xF2F3F5);
    colors[ImGuiCol_TextDisabled] = color(0x8A8D96);
    colors[ImGuiCol_WindowBg] = color(0x0A0A0C);
    colors[ImGuiCol_ChildBg] = color(0x14161C);
    colors[ImGuiCol_PopupBg] = color(0x1B1E26);
    colors[ImGuiCol_Border] = color(0x2A2E38);
    colors[ImGuiCol_FrameBg] = color(0x1F222B);
    colors[ImGuiCol_FrameBgHovered] = color(0x2A2E39);
    colors[ImGuiCol_FrameBgActive] = color(0x333845);
    colors[ImGuiCol_TitleBg] = color(0x0A0A0C);
    colors[ImGuiCol_TitleBgActive] = color(0x0A0A0C);
    colors[ImGuiCol_MenuBarBg] = color(0x0A0A0C);
    colors[ImGuiCol_ScrollbarBg] = color(0x0F1115);
    colors[ImGuiCol_ScrollbarGrab] = color(0x2C313B);
    colors[ImGuiCol_ScrollbarGrabHovered] = color(0x3A404C);
    colors[ImGuiCol_ScrollbarGrabActive] = color(0x4A5160);
    colors[ImGuiCol_CheckMark] = color(0xEC4141);
    colors[ImGuiCol_SliderGrab] = color(0xEC4141);
    colors[ImGuiCol_SliderGrabActive] = color(0xFF5A5A);
    colors[ImGuiCol_Button] = color(0x1F222B);
    colors[ImGuiCol_ButtonHovered] = color(0x2A2E39);
    colors[ImGuiCol_ButtonActive] = color(0x333845);
    colors[ImGuiCol_Header] = color(0x2A1C1E);
    colors[ImGuiCol_HeaderHovered] = color(0x3A2528);
    colors[ImGuiCol_HeaderActive] = color(0xEC4141, 0.25f);
    colors[ImGuiCol_Separator] = color(0x23272F);
    colors[ImGuiCol_TableHeaderBg] = color(0x12141A);
    colors[ImGuiCol_TableBorderStrong] = color(0x23272F);
    colors[ImGuiCol_TableBorderLight] = color(0x1B1E25);
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = color(0x101216);
    colors[ImGuiCol_TextSelectedBg] = color(0xEC4141, 0.35f);
}

void App::setupFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.FontNo = 0;
    config.OversampleH = 1;
    config.OversampleV = 1;

    // 中文界面：优先用系统里的微软雅黑；没有就退回默认字体（中文会显示成方块，但程序不崩）
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
    };
    for (const char* path : candidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(path, 17.0f, &config,
                                                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (font) return;
    }
    io.Fonts->AddFontDefault();
}

void App::setStatus(const std::string& text, bool isError) {
    status_ = text;
    statusError_ = isError;
}

const StoredPlaylist* App::selectedPlaylist() const {
    for (const auto& playlist : store_.playlists()) {
        if (playlist.id == selectedPlaylistId_) return &playlist;
    }
    return nullptr;
}

int64_t App::currentPositionMs() const {
    return seekPreviewMs_ >= 0 ? seekPreviewMs_ : engine_.positionMs();
}

int64_t App::currentLengthMs() const {
    return engine_.lengthMs();
}

void App::playSong(const StoredSong& song) {
    playingSongId_ = song.id;
    nowPlayingTitle_ = song.title;
    seekPreviewMs_ = -1;

    if (!song.localPath.empty()) {
        std::string error;
        if (engine_.playPath(utf8ToWide(song.localPath), &error)) {
            engine_.setVolume(volume_);
            setStatus("正在播放（本地文件）：" + song.title, false);
        } else {
            setStatus("播放失败：" + error, true);
        }
        return;
    }

    setStatus("在线曲目「" + song.title + "」需要 M2 的 WBI 取流，当前版本先跑通本地播放", false);
}

void App::openLocalFile() {
    std::wstring path;
    if (!pickAudioFile(path)) return;
    std::string error;
    if (engine_.playPath(path, &error)) {
        engine_.setVolume(volume_);
        playingSongId_ = -1;
        nowPlayingTitle_ = wideToUtf8(path);
        size_t slash = nowPlayingTitle_.find_last_of("\\/");
        if (slash != std::string::npos) nowPlayingTitle_ = nowPlayingTitle_.substr(slash + 1);
        seekPreviewMs_ = -1;
        setStatus("正在播放（本地文件）：" + nowPlayingTitle_, false);
    } else {
        setStatus("播放失败：" + error, true);
    }
}

void App::draw() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, rootFlags);
    ImGui::PopStyleVar();

    const float bottomBarHeight = 116.0f;
    const float sidebarWidth = 268.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, -bottomBarHeight), ImGuiChildFlags_None);
    drawSidebar();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    ImGui::BeginChild("##content", ImVec2(0, -bottomBarHeight), ImGuiChildFlags_None);
    drawSongList();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 10));
    ImGui::BeginChild("##playbar", ImVec2(0, 0), ImGuiChildFlags_None);
    drawPlayBar();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
}

void App::drawSidebar() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.94f, 0.96f, 1.0f));
    ImGui::TextUnformatted("bilimusic");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("C++ / M1");

    ImGui::Spacing();
    ImGui::TextDisabled("我的歌单（%d）", (int)store_.playlists().size());
    ImGui::Spacing();

    ImGui::BeginChild("##playlistScroll", ImVec2(0, 0), ImGuiChildFlags_None);
    for (const auto& playlist : store_.playlists()) {
        char label[512];
        std::snprintf(label, sizeof(label), "%s  ·  %d 首", playlist.title.c_str(),
                      store_.songCount(playlist.id));
        const bool selected = playlist.id == selectedPlaylistId_;
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0, 28))) {
            selectedPlaylistId_ = playlist.id;
        }
    }
    ImGui::EndChild();
}

void App::drawSongList() {
    const StoredPlaylist* playlist = selectedPlaylist();
    if (!playlist) {
        ImGui::TextDisabled("还没有读到歌单。");
        ImGui::Spacing();
        ImGui::TextWrapped("这个版本和 Java 版共用 %%USERPROFILE%%\\.bilimusic\\playlists.json。"
                           "先用 Java 版登录/导入歌单，再回来这里就能看到。");
        const std::string& status = status_;
        ImGui::Spacing();
        ImGui::TextWrapped("%s", status.c_str());
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.94f, 0.96f, 1.0f));
    ImGui::TextUnformatted(playlist->title.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s · %d 首", sourceLabel(playlist->sourceType),
                        store_.songCount(playlist->id));

    ImGui::Spacing();

    if (ImGui::Button("打开本地文件…")) openLocalFile();

    ImGui::Spacing();

    const auto songs = store_.songsOf(playlist->id);
    if (songs.empty()) {
        ImGui::TextDisabled("这个歌单还没有曲目。");
        return;
    }

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##songs", 4, tableFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("音乐标题", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("本地/在线", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("时长", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableHeadersRow();

        for (size_t index = 0; index < songs.size(); ++index) {
            const StoredSong& song = *songs[index];
            ImGui::TableNextRow();
            const bool isPlaying = song.id == playingSongId_;
            if (isPlaying) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(236, 65, 65, 40));
            }

            bool doubleClicked = false;

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", (int)(index + 1));
            doubleClicked |= ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            ImGui::TableSetColumnIndex(1);
            if (isPlaying) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.25f, 0.25f, 1.0f));
                ImGui::TextUnformatted(song.title.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextUnformatted(song.title.c_str());
            }
            doubleClicked |= ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", song.localPath.empty() ? "在线" : "本地文件");
            doubleClicked |= ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", formatDuration(song.durationMs).c_str());
            doubleClicked |= ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            if (doubleClicked) {
                // 双击延迟到循环结束后处理会影响指针有效性，这里直接调用是安全的：
                // playSong 只访问 song 的字段，不会修改 store_。
                playSong(song);
            }
        }
        ImGui::EndTable();
    }
}

void App::drawPlayBar() {
    const int64_t lengthMs = currentLengthMs();
    const int64_t positionMs = currentPositionMs();
    const int lengthSeconds = (int)(lengthMs / 1000);
    const bool hasMedia = engine_.ready() && lengthSeconds > 0;
    const bool playing = engine_.isPlaying();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.94f, 0.96f, 1.0f));
    ImGui::TextUnformatted(nowPlayingTitle_.empty() ? "还没有在播放" : nowPlayingTitle_.c_str());
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextDisabled("  %s / %s", formatDuration(positionMs).c_str(),
                        formatDuration(lengthMs).c_str());

    ImGui::SameLine();
    const float sliderWidth = ImGui::GetContentRegionAvail().x - 150.0f;
    ImGui::SetNextItemWidth(sliderWidth > 120.0f ? sliderWidth : 120.0f);

    int seekSeconds = (int)(positionMs / 1000);
    if (ImGui::SliderInt("##seek", &seekSeconds, 0, lengthSeconds > 0 ? lengthSeconds : 1, "")) {
        seekPreviewMs_ = (int64_t)seekSeconds * 1000;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && seekPreviewMs_ >= 0) {
        engine_.seekMs(seekPreviewMs_);
        seekPreviewMs_ = -1;
    }

    ImGui::Spacing();

    if (ImGui::Button(playing ? "暂停" : "播放", ImVec2(80, 32))) {
        if (playing) {
            engine_.pause();
        } else {
            engine_.resume();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("停止", ImVec2(80, 32))) {
        engine_.stop();
        seekPreviewMs_ = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("打开文件…", ImVec2(120, 32))) openLocalFile();

    ImGui::SameLine();
    ImGui::TextDisabled("音量");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    int volume = engine_.ready() && engine_.volume() > 0 ? engine_.volume() : volume_;
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
        volume_ = volume;
        engine_.setVolume(volume);
    }

    ImGui::SameLine();
    const ImVec4 statusColor = statusError_ ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                                             : ImVec4(0.55f, 0.58f, 0.65f, 1.0f);
    ImGui::TextColored(statusColor, "%s", status_.c_str());
    (void)hasMedia;
}
