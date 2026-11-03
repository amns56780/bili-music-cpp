#include "data/LibraryStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>

#include <nlohmann/json.hpp>

namespace {

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size, nullptr, nullptr);
    return out;
}

void appendError(std::string* error, const std::string& text) {
    if (!error) return;
    if (!error->empty()) *error += "\n";
    *error += text;
}

// Java 版写出来的 JSON 里可空字段是 null（coverUrl / localPath / sourceTitle …）。
// nlohmann 的 node.value("k", std::string()) 遇到「键存在但值是 null」会抛 type_error，
// 所以这里统一用带 null 判断的读取函数。
std::string readString(const nlohmann::json& node, const char* key) {
    auto it = node.find(key);
    if (it == node.end() || it->is_null() || !it->is_string()) return {};
    return it->get<std::string>();
}

int64_t readInt64(const nlohmann::json& node, const char* key) {
    auto it = node.find(key);
    if (it == node.end() || it->is_null()) return 0;
    if (it->is_number_integer()) return it->get<int64_t>();
    if (it->is_number_unsigned()) return (int64_t)it->get<uint64_t>();
    return 0;
}

int readInt(const nlohmann::json& node, const char* key) {
    return (int)readInt64(node, key);
}

}  // namespace

std::wstring LibraryStore::dataDir() {
    wchar_t buffer[32768] = {};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", buffer, (DWORD)(sizeof(buffer) / sizeof(wchar_t)));
    if (len == 0) return L".bilimusic";
    return std::wstring(buffer, len) + L"\\.bilimusic";
}

std::wstring LibraryStore::playlistsFile() {
    return dataDir() + L"\\playlists.json";
}

bool LibraryStore::load(std::string* error) {
    playlists_.clear();
    songs_.clear();
    loaded_ = false;

    // 用 _wfopen 读：MinGW 的 std::ifstream 没有 wchar_t 路径重载，
    // 而用户目录可能含中文，走 std::ifstream(std::string) 会按 ANSI 解释路径出错。
    std::FILE* file = _wfopen(playlistsFile().c_str(), L"rb");
    if (!file) {
        appendError(error, "没有找到歌单文件：" + wideToUtf8(playlistsFile()) +
                               "（首次使用需要先用 Java 版登录/导入，或后续版本内置导入）");
        return false;
    }
    std::string content;
    if (std::fseek(file, 0, SEEK_END) == 0) {
        long size = std::ftell(file);
        if (size > 0) {
            content.resize((size_t)size);
            std::rewind(file);
            size_t read = std::fread(content.data(), 1, content.size(), file);
            content.resize(read);
        }
    }
    std::fclose(file);

    nlohmann::json document;
    try {
        document = nlohmann::json::parse(content);
    } catch (const std::exception& exception) {
        appendError(error, std::string("playlists.json 解析失败：") + exception.what());
        return false;
    }

    if (document.contains("playlists") && document["playlists"].is_array()) {
        for (const auto& node : document["playlists"]) {
            StoredPlaylist playlist;
            playlist.id = readInt64(node, "id");
            playlist.createdAt = readInt64(node, "createdAt");
            playlist.title = readString(node, "title");
            playlist.sourceType = readString(node, "sourceType");
            playlist.sourceTitle = readString(node, "sourceTitle");
            playlists_.push_back(std::move(playlist));
        }
    }
    if (document.contains("songs") && document["songs"].is_array()) {
        for (const auto& node : document["songs"]) {
            StoredSong song;
            song.id = readInt64(node, "id");
            song.playlistId = readInt64(node, "playlistId");
            song.cid = readInt64(node, "cid");
            song.durationMs = readInt64(node, "durationMs");
            song.sortOrder = readInt(node, "sortOrder");
            song.bvid = readString(node, "bvid");
            song.title = readString(node, "title");
            song.artist = readString(node, "artist");
            song.coverUrl = readString(node, "coverUrl");
            song.localPath = readString(node, "localPath");
            songs_.push_back(std::move(song));
        }
    }

    std::sort(songs_.begin(), songs_.end(), [](const StoredSong& a, const StoredSong& b) {
        if (a.playlistId != b.playlistId) return a.playlistId < b.playlistId;
        return a.sortOrder < b.sortOrder;
    });
    loaded_ = true;
    return true;
}

std::vector<const StoredSong*> LibraryStore::songsOf(int64_t playlistId) const {
    std::vector<const StoredSong*> result;
    for (const auto& song : songs_) {
        if (song.playlistId == playlistId) result.push_back(&song);
    }
    return result;
}

int LibraryStore::songCount(int64_t playlistId) const {
    int count = 0;
    for (const auto& song : songs_) {
        if (song.playlistId == playlistId) ++count;
    }
    return count;
}
