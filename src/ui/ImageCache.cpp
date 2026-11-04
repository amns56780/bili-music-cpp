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

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "net/HttpClient.h"
#include "ui/AppIcon.h"
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

ImTextureID AppIconTexture() {
    static ImTextureID cached = 0;
    static bool tried = false;
    if (tried) return cached;
    tried = true;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const int size = Ui::AppIcon::kImages[0].width;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 Ui::AppIcon::kImages[0].pixels);
    cached = (ImTextureID)(intptr_t)texture;
    return cached;
}

bool CoverTextureSize(ImTextureID texture, int* width, int* height) {
    if (texture == 0) return false;
    std::lock_guard<std::mutex> lock(gMutex);
    for (const auto& item : gEntries) {
        if (item.second.texture != texture) continue;
        if (width != nullptr) *width = item.second.width;
        if (height != nullptr) *height = item.second.height;
        return true;
    }
    return false;
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


namespace {

std::wstring readWallpaperPathW() {
    DWORD type = 0;
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper", RRF_RT_REG_SZ,
                     &type, nullptr, &size) != ERROR_SUCCESS ||
        size == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper", RRF_RT_REG_SZ,
                     &type, buffer.data(), &size) != ERROR_SUCCESS) {
        return {};
    }
    return std::wstring(buffer.data());
}

std::string wideToUtf8Local(const std::wstring& text) {
    if (text.empty()) return {};
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size, nullptr,
                        nullptr);
    return out;
}

unsigned char* loadDownscaled(const std::vector<unsigned char>& bytes, int maxWidth, int* outWidth,
                              int* outHeight) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(bytes.data(), (int)bytes.size(), &width, &height, &channels, 4);
    if (pixels == nullptr) return nullptr;
    if (width <= maxWidth) {
        *outWidth = width;
        *outHeight = height;
        return pixels;
    }
    const float scale = (float)maxWidth / (float)width;
    int newWidth = maxWidth;
    int newHeight = (int)(height * scale);
    if (newHeight < 1) newHeight = 1;
    auto* scaled = (unsigned char*)malloc((size_t)newWidth * newHeight * 4);
    for (int y = 0; y < newHeight; ++y) {
        const int srcY = (int)(y / scale);
        for (int x = 0; x < newWidth; ++x) {
            const int srcX = (int)(x / scale);
            memcpy(scaled + ((size_t)y * newWidth + x) * 4,
                   pixels + ((size_t)srcY * width + srcX) * 4, 4);
        }
    }
    stbi_image_free(pixels);
    *outWidth = newWidth;
    *outHeight = newHeight;
    return scaled;
}

void boxBlurPass(unsigned char* rgba, int width, int height, int radius) {
    std::vector<unsigned char> temp((size_t)width * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int x0 = std::max(0, x - radius);
            const int x1 = std::min(width - 1, x + radius);
            int count = x1 - x0 + 1;
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            for (int sx = x0; sx <= x1; ++sx) {
                const unsigned char* src = rgba + ((size_t)y * width + sx) * 4;
                sumR += src[0];
                sumG += src[1];
                sumB += src[2];
                sumA += src[3];
            }
            unsigned char* dst = temp.data() + ((size_t)y * width + x) * 4;
            dst[0] = (unsigned char)(sumR / count);
            dst[1] = (unsigned char)(sumG / count);
            dst[2] = (unsigned char)(sumB / count);
            dst[3] = (unsigned char)(sumA / count);
        }
    }
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            const int y0 = std::max(0, y - radius);
            const int y1 = std::min(height - 1, y + radius);
            int count = y1 - y0 + 1;
            int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            for (int sy = y0; sy <= y1; ++sy) {
                const unsigned char* src = temp.data() + ((size_t)sy * width + x) * 4;
                sumR += src[0];
                sumG += src[1];
                sumB += src[2];
                sumA += src[3];
            }
            unsigned char* dst = rgba + ((size_t)y * width + x) * 4;
            dst[0] = (unsigned char)(sumR / count);
            dst[1] = (unsigned char)(sumG / count);
            dst[2] = (unsigned char)(sumB / count);
            dst[3] = (unsigned char)(sumA / count);
        }
    }
}

ImTextureID makeBlurredTexture(const std::string& path, const std::string& cacheKey) {
    static std::map<std::string, ImTextureID> cache;
    static std::set<std::string> failed;
    auto it = cache.find(cacheKey);
    if (it != cache.end()) return it->second;
    if (failed.count(cacheKey) != 0) return (ImTextureID)0;
    const std::vector<unsigned char> bytes = readWholeFile(path);
    if (bytes.empty()) {
        failed.insert(cacheKey);
        return (ImTextureID)0;
    }
    int width = 0;
    int height = 0;
    unsigned char* pixels = loadDownscaled(bytes, 1280, &width, &height);
    if (pixels == nullptr) {
        failed.insert(cacheKey);
        return (ImTextureID)0;
    }
    const int radius = std::max(4, std::min(24, std::min(width, height) / 60));
    boxBlurPass(pixels, width, height, radius);
    boxBlurPass(pixels, width, height, radius);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
    cache[cacheKey] = (ImTextureID)(intptr_t)texture;
    AppLog::info("ImageCache", "模糊背景纹理 " + std::to_string(width) + "x" +
                                   std::to_string(height) + " " + path);
    return cache[cacheKey];
}

}  // namespace

ImTextureID BlurredWallpaperTexture() {
    const std::wstring path = readWallpaperPathW();
    if (path.empty()) return (ImTextureID)0;
    const std::string utf8 = wideToUtf8Local(path);
    return makeBlurredTexture(utf8, "wallpaper:" + utf8);
}

ImTextureID BlurredImageTexture(const std::string& path) {
    if (path.empty()) return (ImTextureID)0;
    return makeBlurredTexture(path, "image:" + path);
}

}  // namespace Ui
