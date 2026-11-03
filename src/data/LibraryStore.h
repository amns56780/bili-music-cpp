#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 与 Java 版共用同一份数据文件：%USERPROFILE%\.bilimusic\playlists.json
// 字段名保持完全一致，这样 C++ 版和 Java 版可以来回切换、不打架。
struct StoredSong {
    int64_t id = 0;
    int64_t playlistId = 0;
    int64_t cid = 0;
    int64_t durationMs = 0;
    int sortOrder = 0;
    std::string bvid;
    std::string title;
    std::string artist;
    std::string coverUrl;
    std::string localPath;   // 非空 = 本地文件，可直接交给 VLC 播
};

struct StoredPlaylist {
    int64_t id = 0;
    int64_t createdAt = 0;
    std::string title;
    std::string sourceType;
    std::string sourceTitle;
};

class LibraryStore {
public:
    bool load(std::string* error);
    bool loaded() const { return loaded_; }

    const std::vector<StoredPlaylist>& playlists() const { return playlists_; }
    const std::vector<StoredSong>& songs() const { return songs_; }

    std::vector<const StoredSong*> songsOf(int64_t playlistId) const;
    int songCount(int64_t playlistId) const;

    static std::wstring dataDir();
    static std::wstring playlistsFile();

private:
    std::vector<StoredPlaylist> playlists_;
    std::vector<StoredSong> songs_;
    bool loaded_ = false;
};
