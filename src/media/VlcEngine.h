#pragma once

#include <cstdint>
#include <string>

// libVLC 播放内核封装。
//
// 设计取舍：不链接 vlc 的 .lib，而是运行时 LoadLibrary + GetProcAddress。
//   1) 打包时用户机器上有没有 VLC 的 SDK 都无所谓，只要有 libvlc.dll；
//   2) 可以按「程序目录/vlc → 系统安装的 VLC」顺序找内核，和 Java 版的行为一致。
// 等 UI 稳定后再换成官方头文件（include/vlc/*.h）以拿到完整结构体/事件。
class VlcEngine {
public:
    VlcEngine();
    ~VlcEngine();

    VlcEngine(const VlcEngine&) = delete;
    VlcEngine& operator=(const VlcEngine&) = delete;

    // 加载 libvlc 并创建实例；失败时 error 里是排查信息（尝试过哪些路径）
    bool init(std::string* error);
    void shutdown();

    bool ready() const { return instance_ != nullptr; }
    std::string version() const;            // 例如 "3.0.21 Vetinari"
    std::wstring libraryDir() const { return libraryDir_; }

    bool playPath(const std::wstring& path, std::string* error);                     // 本地文件
    bool playUrl(const std::string& url, const std::string& referrer,
                 const std::string& userAgent, std::string* error);                  // 网络流

    void togglePause();
    void pause();
    void resume();
    void stop();

    bool isPlaying() const;
    int64_t positionMs() const;
    int64_t lengthMs() const;
    void seekMs(int64_t ms);

    int volume() const;
    void setVolume(int value);

private:
    void releasePlayer();

    void* library_ = nullptr;                 // HMODULE
    void* instance_ = nullptr;                // libvlc_instance_t*
    void* media_ = nullptr;                   // libvlc_media_t*
    void* player_ = nullptr;                  // libvlc_media_player_t*
    std::wstring libraryDir_;

    struct Api;
    Api* api_ = nullptr;
};
