#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 与 Java 版共用同一份数据文件：%USERPROFILE%\.bilimusic\playlists.json
// 字段名、可空字段的写法都保持一致，这样 C++ 版和 Java 版可以来回切换、不打架。
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
    std::string sourceType;   // FAV / TOVIEW / UPLOAD / SEASON / LOCAL / MANUAL
    std::string sourceTitle;  // 导入来源的原始标题（用于重复导入时认亲）
    std::string coverUrl;
    std::string coverPath;
};

// 重名判断只需要「标题 + 曲目数」
struct ExistingPlaylistTitle {
    std::string title;
    int songCount = 0;
};

struct PlaylistNameConflict {
    std::string desiredTitle;
    std::string existingTitle;
    std::string suggestedTitle;
    int existingSongCount = 0;
};

class LibraryStore {
public:
    bool load(std::string* error);
    bool save(std::string* error = nullptr);
    bool loaded() const { return loaded_; }

    const std::vector<StoredPlaylist>& playlists() const { return playlists_; }
    const std::vector<StoredSong>& songs() const { return songs_; }

    std::vector<const StoredSong*> songsOf(int64_t playlistId) const;
    int songCount(int64_t playlistId) const;

    // ---------------- 重名规则（与 Java 版 PlaylistNaming 一致） ----------------
    static std::string normalizeTitle(const std::string& rawTitle);  // 空字符串 = 输入非法
    static std::string stripTitleSuffix(const std::string& title);   // 没有 "(n)" 后缀返回空串
    static std::string uniqueTitle(const std::string& desired,
                                   const std::vector<std::string>& existingTitles);

    const StoredPlaylist* findByTitle(const std::string& title, int64_t exceptId = -1) const;
    const StoredPlaylist* findBySource(const std::string& sourceType,
                                      const std::string& title) const;
    std::vector<std::string> titles() const;
    bool findConflict(const std::string& desired, int64_t exceptId,
                      PlaylistNameConflict* conflict) const;

    // ---------------- 写操作（每次都会落盘） ----------------
    int64_t createPlaylist(const std::string& title, const std::string& sourceType = "MANUAL",
                           const std::string& sourceTitle = "");
    int64_t createPlaylistOverwriting(const std::string& title,
                                      const std::string& sourceType = "MANUAL",
                                      const std::string& sourceTitle = "");
    bool renamePlaylist(int64_t id, const std::string& title);
    bool deletePlaylist(int64_t id);
    int deletePlaylistByTitle(const std::string& title, int64_t exceptId = -1);
    // incoming 只需要 bvid/cid/title/artist/coverUrl/durationMs/localPath
    int addSongs(int64_t playlistId, const std::vector<StoredSong>& incoming);
    int removeSongs(const std::vector<int64_t>& songIds);
    int copySongsTo(int64_t targetPlaylistId, const std::vector<int64_t>& songIds);
    int moveSongsTo(int64_t targetPlaylistId, const std::vector<int64_t>& songIds);
    bool setPlaylistCover(int64_t id, const std::string& coverUrl, const std::string& coverPath);
    bool setSongLocalPath(int64_t songId, const std::string& localPath);
    bool setSongCover(int64_t songId, const std::string& coverUrl);

    static std::wstring dataDir();
    static std::wstring playlistsFile();

private:
    std::vector<StoredPlaylist> playlists_;
    std::vector<StoredSong> songs_;
    int64_t nextPlaylistId_ = 1;
    int64_t nextSongId_ = 1;
    int titleMigrationVersion_ = 0;
    bool loaded_ = false;
};
