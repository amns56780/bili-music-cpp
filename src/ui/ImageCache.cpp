#include "ui/ImageCache.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "net/HttpClient.h"
#include "util/AppLog.h"

namespace Ui {

namespace {

constexpr int kMaxConcurrentDownloads = 6;
constexpr size_t kMaxTextures = 160;

struct Entry {
    ImTextureID texture = 0;
    std::vector<unsigned char> readyBytes;
    int width = 0;
    int height = 0;
    bool ready = false;   // 字节已就绪，等待主线程上传
    bool started = false; // 已发起下载
    bool failed = false;
};

std::mutex gMutex;
std::map<std::string, Entry> gEntries;
std::atomic<int> gInFlight{0};

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size);
    return out;
}

std::vector<unsigned char> readWholeFile(const std::string& path) {
    std::vector<unsigned char> bytes;
    std::FILE* file = _wfopen(utf8ToWide(path).c_str(), L"rb");
    if (file == nullptr) return bytes;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        bytes.resize((size_t)size);
        const size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
        bytes.resize(read);
    }
    std::fclose(file);
    return bytes;
}

void uploadTexture(Entry& entry) {
    if (entry.readyBytes.empty()) {
        entry.failed = true;
        return;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(entry.readyBytes.data(),
                                                  (int)entry.readyBytes.size(), &width, &height,
                                                  &channels, 4);
    entry.readyBytes.clear();
    entry.readyBytes.shrink_to_fit();
    if (pixels == nullptr) {
        entry.failed = true;
        return;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    entry.texture = (ImTextureID)(intptr_t)texture;
    entry.width = width;
    entry.height = height;
    entry.ready = false;
    AppLog::info("ImageCache", "封面上传成功 " + std::to_string(width) + "x" +
                                   std::to_string(height));
}

void startDownload(const std::string& url) {
    gInFlight.fetch_add(1);
    std::thread([url]() {
        net::HttpResponse response;
        std::string error;
        const std::vector<std::pair<std::string, std::string>> headers = {
            {"User-Agent",
             "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
             "Chrome/126.0.0.0 Safari/537.36"},
            {"Referer", "https://www.bilibili.com/"},
        };
        const bool ok = net::httpGet(url, headers, &response, &error);
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gEntries.find(url);
        if (it != gEntries.end()) {
            if (ok && !response.body.empty()) {
                it->second.readyBytes.assign(response.body.begin(), response.body.end());
                it->second.ready = true;
            } else {
                it->second.failed = true;
                if (!ok) AppLog::info("ImageCache", "封面下载失败 " + error);
            }
        }
        gInFlight.fetch_sub(1);
    }).detach();
}

}  // namespace

ImTextureID CoverTextureFor(const std::string& coverUrl, const std::string& coverPath) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!coverPath.empty()) {
        const std::string key = "path:" + coverPath;
        auto it = gEntries.find(key);
        if (it != gEntries.end()) return it->second.texture;
        if (gEntries.size() > kMaxTextures) return 0;
        Entry entry;
        entry.readyBytes = readWholeFile(coverPath);
        gEntries[key] = std::move(entry);
        auto inserted = gEntries.find(key);
        uploadTexture(inserted->second);
        return inserted->second.texture;
    }
    if (coverUrl.empty()) return 0;
    auto it = gEntries.find(coverUrl);
    if (it != gEntries.end()) {
        if (it->second.ready) uploadTexture(it->second);
        return it->second.texture;
    }
    if (gEntries.size() > kMaxTextures) return 0;
    if (gInFlight.load() >= kMaxConcurrentDownloads) return 0;
    Entry entry;
    entry.started = true;
    gEntries[coverUrl] = std::move(entry);
    startDownload(coverUrl);
    return 0;
}

void PumpImageCache() {
    std::lock_guard<std::mutex> lock(gMutex);
    for (auto& item : gEntries) {
        if (item.second.ready) uploadTexture(item.second);
    }
}

void ClearImageCache() {
    std::lock_guard<std::mutex> lock(gMutex);
    for (auto& item : gEntries) {
        if (item.second.texture != 0) {
            GLuint texture = (GLuint)(intptr_t)item.second.texture;
            glDeleteTextures(1, &texture);
        }
    }
    gEntries.clear();
}

}  // namespace Ui
