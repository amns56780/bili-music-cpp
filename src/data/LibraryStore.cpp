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
#include <ctime>
#include <set>

#include <nlohmann/json.hpp>

#include "util/AppLog.h"

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

const nlohmann::json& nullIfEmpty(const std::string& value, nlohmann::json* storage) {
    if (value.empty()) {
        *storage = nullptr;
    } else {
        *storage = value;
    }
    return *storage;
}

std::string trim(const std::string& text) {
    const char* whitespace = " \t\r\n";
    const size_t begin = text.find_first_not_of(whitespace);
    if (begin == std::string::npos) return {};
    const size_t end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

std::string trimEnd(const std::string& text) {
    const size_t end = text.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return {};
    return text.substr(0, end + 1);
}

bool allDigits(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// 识别末尾的 (n) / （n），返回根标题；没有后缀返回空串
std::string stripSuffix(const std::string& title) {
    const std::string trimmed = trim(title);
    if (trimmed.empty()) return {};
    const size_t closePos = trimmed.size() - 1;
    if (trimmed[closePos] != ')' && trimmed[closePos] != '\xEF') return {};
    // 处理全角右括号）——UTF-8: EF BC 89
    size_t closeLen = 1;
    if (trimmed.size() >= 3 && trimmed.compare(trimmed.size() - 3, 3, "\xEF\xBC\x89") == 0) {
        closeLen = 3;
    } else if (trimmed[closePos] != ')') {
        return {};
    }
    const size_t contentEnd = trimmed.size() - closeLen;
    // 找左括号
    size_t openPos = std::string::npos;
    size_t openLen = 1;
    for (size_t i = contentEnd; i > 0; --i) {
        if (i >= 3 && trimmed.compare(i - 3, 3, "\xEF\xBC\x88") == 0) {
            openPos = i - 3;
            openLen = 3;
            break;
        }
        if (trimmed[i - 1] == '(') {
            openPos = i - 1;
            openLen = 1;
            break;
        }
    }
    if (openPos == std::string::npos) return {};
    const std::string digits = trimmed.substr(openPos + openLen, contentEnd - (openPos + openLen));
    if (!allDigits(digits)) return {};
    const std::string root = trimEnd(trimmed.substr(0, openPos));
    if (root.empty()) return {};
    return root;
}

int64_t nowEpochMs() {
    FILETIME fileTime{};
    GetSystemTimeAsFileTime(&fileTime);
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    // 100ns since 1601-01-01 → ms since 1970-01-01
    return (int64_t)((value.QuadPart - 116444736000000000ULL) / 10000ULL);
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
    nextPlaylistId_ = 1;
    nextSongId_ = 1;
    titleMigrationVersion_ = 0;
    loaded_ = false;

    // 用 _wfopen 读：MinGW 的 std::ifstream 没有 wchar_t 路径重载，
    // 而用户目录可能含中文，走 std::ifstream(std::string) 会按 ANSI 解释路径出错。
    std::FILE* file = _wfopen(playlistsFile().c_str(), L"rb");
    if (!file) {
        appendError(error, "没有找到歌单文件：" + wideToUtf8(playlistsFile()));
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
            playlist.coverUrl = readString(node, "coverUrl");
            playlist.coverPath = readString(node, "coverPath");
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
    nextPlaylistId_ = document.value("nextPlaylistId", (int64_t)1);
    nextSongId_ = document.value("nextSongId", (int64_t)1);
    titleMigrationVersion_ = (int)document.value("titleMigrationVersion", 0);
    if (nextPlaylistId_ <= 0) nextPlaylistId_ = 1;
    if (nextSongId_ <= 0) nextSongId_ = 1;

    std::sort(songs_.begin(), songs_.end(), [](const StoredSong& a, const StoredSong& b) {
        if (a.playlistId != b.playlistId) return a.playlistId < b.playlistId;
        return a.sortOrder < b.sortOrder;
    });
    loaded_ = true;
    return true;
}

bool LibraryStore::save(std::string* error) {
    nlohmann::json document = nlohmann::json::object();
    nlohmann::json playlistsJson = nlohmann::json::array();
    for (const StoredPlaylist& playlist : playlists_) {
        nlohmann::json node = nlohmann::json::object();
        node["id"] = playlist.id;
        node["title"] = playlist.title;
        node["sourceType"] = playlist.sourceType.empty() ? "MANUAL" : playlist.sourceType;
        node["createdAt"] = playlist.createdAt;
        nlohmann::json scratch;
        node["coverUrl"] = nullIfEmpty(playlist.coverUrl, &scratch);
        node["coverPath"] = nullIfEmpty(playlist.coverPath, &scratch);
        node["sourceTitle"] = nullIfEmpty(playlist.sourceTitle, &scratch);
        playlistsJson.push_back(std::move(node));
    }
    nlohmann::json songsJson = nlohmann::json::array();
    for (const StoredSong& song : songs_) {
        nlohmann::json node = nlohmann::json::object();
        node["id"] = song.id;
        node["playlistId"] = song.playlistId;
        node["bvid"] = song.bvid;
        node["cid"] = song.cid;
        node["title"] = song.title;
        node["artist"] = song.artist;
        nlohmann::json scratch;
        node["coverUrl"] = nullIfEmpty(song.coverUrl, &scratch);
        node["durationMs"] = song.durationMs;
        node["sortOrder"] = song.sortOrder;
        node["localPath"] = nullIfEmpty(song.localPath, &scratch);
        songsJson.push_back(std::move(node));
    }
    document["playlists"] = std::move(playlistsJson);
    document["songs"] = std::move(songsJson);
    document["nextPlaylistId"] = nextPlaylistId_;
    document["nextSongId"] = nextSongId_;
    document["titleMigrationVersion"] = titleMigrationVersion_;

    std::string content;
    try {
        content = document.dump(2);
    } catch (const std::exception& exception) {
        appendError(error, std::string("playlists.json 序列化失败：") + exception.what());
        return false;
    }

    CreateDirectoryW(dataDir().c_str(), nullptr);
    const std::wstring target = playlistsFile();
    const std::wstring temporary = target + L".tmp";
    std::FILE* file = _wfopen(temporary.c_str(), L"wb");
    if (!file) {
        appendError(error, "无法写入临时文件：" + wideToUtf8(temporary));
        return false;
    }
    const size_t written = std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
    if (written != content.size()) {
        appendError(error, "写入 playlists.json.tmp 不完整");
        _wremove(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // 某些环境下 MoveFileEx 被占用，退回「删了再改名」
        _wremove(target.c_str());
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            appendError(error, "替换 playlists.json 失败");
            _wremove(temporary.c_str());
            return false;
        }
    }
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

// ---------------- 重名规则 ----------------

std::string LibraryStore::normalizeTitle(const std::string& rawTitle) {
    const std::string trimmed = trim(rawTitle);
    if (trimmed.empty()) return {};
    const std::string root = stripSuffix(trimmed);
    if (root.empty()) return trimmed;
    // 把「（2）」这种全角写法统一成半角 " (2)"
    const std::string suffixSource = trimmed.substr(trimmed.size() - 1) == ")" ? "" : "";
    (void)suffixSource;
    // 提取末尾数字
    size_t digitEnd = trimmed.size() - 1;
    if (trimmed.size() >= 3 && trimmed.compare(trimmed.size() - 3, 3, "\xEF\xBC\x89") == 0) {
        digitEnd = trimmed.size() - 3;
    }
    size_t digitStart = digitEnd;
    while (digitStart > 0 && trimmed[digitStart - 1] >= '0' && trimmed[digitStart - 1] <= '9') {
        --digitStart;
    }
    return root + " (" + trimmed.substr(digitStart, digitEnd - digitStart) + ")";
}

std::string LibraryStore::stripTitleSuffix(const std::string& title) {
    return stripSuffix(title);
}

std::string LibraryStore::uniqueTitle(const std::string& desired,
                                     const std::vector<std::string>& existingTitles) {
    std::vector<std::string> normalized;
    normalized.reserve(existingTitles.size());
    for (const std::string& title : existingTitles) {
        const std::string value = normalizeTitle(title);
        if (!value.empty()) normalized.push_back(value);
    }
    auto taken = [&](const std::string& title) {
        return std::find(normalized.begin(), normalized.end(), title) != normalized.end();
    };
    std::string base = normalizeTitle(desired);
    if (base.empty()) base = "未命名歌单";
    if (!taken(base)) return base;
    std::string root = stripSuffix(base);
    if (root.empty()) root = base;
    int index = 2;
    while (taken(root + " (" + std::to_string(index) + ")")) ++index;
    return root + " (" + std::to_string(index) + ")";
}

const StoredPlaylist* LibraryStore::findByTitle(const std::string& title, int64_t exceptId) const {
    const std::string desired = normalizeTitle(title);
    if (desired.empty()) return nullptr;
    for (const StoredPlaylist& playlist : playlists_) {
        if (exceptId >= 0 && playlist.id == exceptId) continue;
        if (normalizeTitle(playlist.title) == desired) return &playlist;
    }
    return nullptr;
}

const StoredPlaylist* LibraryStore::findBySource(const std::string& sourceType,
                                                const std::string& title) const {
    const std::string desired = normalizeTitle(title);
    if (desired.empty()) return nullptr;
    for (const StoredPlaylist& playlist : playlists_) {
        if (playlist.sourceType != sourceType) continue;
        const std::string candidate =
            normalizeTitle(playlist.sourceTitle.empty() ? playlist.title : playlist.sourceTitle);
        if (candidate == desired) return &playlist;
    }
    return nullptr;
}

std::vector<std::string> LibraryStore::titles() const {
    std::vector<std::string> result;
    result.reserve(playlists_.size());
    for (const StoredPlaylist& playlist : playlists_) result.push_back(playlist.title);
    return result;
}

bool LibraryStore::findConflict(const std::string& desired, int64_t exceptId,
                                PlaylistNameConflict* conflict) const {
    if (conflict == nullptr) return false;
    const std::string normalized = normalizeTitle(desired);
    if (normalized.empty()) return false;
    const StoredPlaylist* matched = findByTitle(normalized, exceptId);
    if (matched == nullptr) return false;
    conflict->desiredTitle = normalized;
    conflict->existingTitle = matched->title;
    conflict->existingSongCount = songCount(matched->id);
    conflict->suggestedTitle = uniqueTitle(normalized, titles());
    return true;
}

// ---------------- 写操作 ----------------

int64_t LibraryStore::createPlaylist(const std::string& title, const std::string& sourceType,
                                     const std::string& sourceTitle) {
    std::string normalized = normalizeTitle(title);
    if (normalized.empty()) normalized = "未命名歌单";
    StoredPlaylist playlist;
    playlist.id = nextPlaylistId_++;
    playlist.title = normalized;
    playlist.sourceType = sourceType.empty() ? "MANUAL" : sourceType;
    const std::string normalizedSource = normalizeTitle(sourceTitle);
    playlist.sourceTitle = normalizedSource;
    playlist.createdAt = nowEpochMs();
    playlists_.push_back(std::move(playlist));
    save(nullptr);
    return playlists_.back().id;
}

int64_t LibraryStore::createPlaylistOverwriting(const std::string& title,
                                                const std::string& sourceType,
                                                const std::string& sourceTitle) {
    deletePlaylistByTitle(title);
    return createPlaylist(title, sourceType, sourceTitle);
}

bool LibraryStore::renamePlaylist(int64_t id, const std::string& title) {
    const std::string normalized = normalizeTitle(title);
    if (normalized.empty()) return false;
    for (StoredPlaylist& playlist : playlists_) {
        if (playlist.id != id) continue;
        playlist.title = normalized;
        save(nullptr);
        return true;
    }
    return false;
}

bool LibraryStore::deletePlaylist(int64_t id) {
    const size_t before = playlists_.size();
    playlists_.erase(std::remove_if(playlists_.begin(), playlists_.end(),
                                    [id](const StoredPlaylist& p) { return p.id == id; }),
                     playlists_.end());
    songs_.erase(std::remove_if(songs_.begin(), songs_.end(),
                                [id](const StoredSong& s) { return s.playlistId == id; }),
                 songs_.end());
    if (playlists_.size() == before) return false;
    save(nullptr);
    return true;
}

int LibraryStore::deletePlaylistByTitle(const std::string& title, int64_t exceptId) {
    const std::string desired = normalizeTitle(title);
    if (desired.empty()) return 0;
    std::vector<int64_t> ids;
    for (const StoredPlaylist& playlist : playlists_) {
        if (exceptId >= 0 && playlist.id == exceptId) continue;
        if (normalizeTitle(playlist.title) == desired) ids.push_back(playlist.id);
    }
    if (ids.empty()) return 0;
    for (int64_t id : ids) {
        playlists_.erase(std::remove_if(playlists_.begin(), playlists_.end(),
                                        [id](const StoredPlaylist& p) { return p.id == id; }),
                         playlists_.end());
        songs_.erase(std::remove_if(songs_.begin(), songs_.end(),
                                    [id](const StoredSong& s) { return s.playlistId == id; }),
                     songs_.end());
    }
    save(nullptr);
    return (int)ids.size();
}

int LibraryStore::addSongs(int64_t playlistId, const std::vector<StoredSong>& incoming) {
    // 在线曲目按 (bvid, cid) 去重；本地音乐（没有 bvid）按 localPath 去重
    std::vector<std::pair<std::string, int64_t>> existingKeys;
    std::vector<std::string> existingLocalPaths;
    int maxOrder = -1;
    for (const StoredSong& song : songs_) {
        if (song.playlistId != playlistId) continue;
        if (song.bvid.empty()) {
            existingLocalPaths.push_back(song.localPath);
        } else {
            existingKeys.emplace_back(song.bvid, song.cid);
        }
        if (song.sortOrder > maxOrder) maxOrder = song.sortOrder;
    }
    int order = maxOrder + 1;
    int added = 0;
    for (const StoredSong& song : incoming) {
        if (song.bvid.empty()) {
            if (song.localPath.empty()) continue;
            if (std::find(existingLocalPaths.begin(), existingLocalPaths.end(), song.localPath) !=
                existingLocalPaths.end()) {
                continue;
            }
            existingLocalPaths.push_back(song.localPath);
        } else {
            const std::pair<std::string, int64_t> key(song.bvid, song.cid);
            if (std::find(existingKeys.begin(), existingKeys.end(), key) != existingKeys.end()) {
                continue;
            }
            existingKeys.push_back(key);
        }
        StoredSong stored = song;
        stored.id = nextSongId_++;
        stored.playlistId = playlistId;
        stored.sortOrder = order++;
        songs_.push_back(std::move(stored));
        ++added;
    }
    if (added > 0) {
        std::sort(songs_.begin(), songs_.end(), [](const StoredSong& a, const StoredSong& b) {
            if (a.playlistId != b.playlistId) return a.playlistId < b.playlistId;
            return a.sortOrder < b.sortOrder;
        });
        save(nullptr);
    }
    return added;
}

int LibraryStore::removeSongs(const std::vector<int64_t>& songIds) {
    if (songIds.empty()) return 0;
    std::set<int64_t> ids(songIds.begin(), songIds.end());
    const size_t before = songs_.size();
    songs_.erase(std::remove_if(songs_.begin(), songs_.end(),
                                [&ids](const StoredSong& song) { return ids.count(song.id) != 0; }),
                 songs_.end());
    const int removed = (int)(before - songs_.size());
    if (removed > 0) save(nullptr);
    return removed;
}

int LibraryStore::copySongsTo(int64_t targetPlaylistId, const std::vector<int64_t>& songIds) {
    std::set<int64_t> ids(songIds.begin(), songIds.end());
    std::vector<StoredSong> picked;
    for (const StoredSong& song : songs_) {
        if (ids.count(song.id) == 0) continue;
        StoredSong copy = song;
        copy.id = 0;
        copy.playlistId = 0;
        copy.sortOrder = 0;
        picked.push_back(std::move(copy));
    }
    if (picked.empty()) return 0;
    return addSongs(targetPlaylistId, picked);
}

int LibraryStore::moveSongsTo(int64_t targetPlaylistId, const std::vector<int64_t>& songIds) {
    const int added = copySongsTo(targetPlaylistId, songIds);
    removeSongs(songIds);
    return added;
}


bool LibraryStore::setSongLocalPath(int64_t songId, const std::string& localPath) {
    for (StoredSong& song : songs_) {
        if (song.id != songId) continue;
        song.localPath = localPath;
        save(nullptr);
        return true;
    }
    return false;
}

bool LibraryStore::setPlaylistCover(int64_t id, const std::string& coverUrl,
                                    const std::string& coverPath) {
    for (StoredPlaylist& playlist : playlists_) {
        if (playlist.id != id) continue;
        playlist.coverUrl = coverUrl;
        playlist.coverPath = coverPath;
        save(nullptr);
        return true;
    }
    return false;
}

