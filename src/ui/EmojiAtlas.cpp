#include "ui/EmojiAtlas.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "util/AppLog.h"

namespace Ui {

namespace {

constexpr int kTextureSize = 64;
constexpr float kFontSize = 46.0f;

IWICImagingFactory* gWic = nullptr;
ID2D1Factory* gD2d = nullptr;
IDWriteFactory* gDwrite = nullptr;
bool gInitTried = false;
bool gInitOk = false;
std::unordered_map<std::string, unsigned int> gCache;

bool ensureFactories() {
    if (gInitTried) return gInitOk;
    gInitTried = true;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        AppLog::warn("Emoji", "CoInitializeEx 失败：" + std::to_string((long)com));
        return false;
    }
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&gWic)))) {
        AppLog::warn("Emoji", "WIC 工厂创建失败");
        return false;
    }
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &gD2d))) {
        AppLog::warn("Emoji", "D2D 工厂创建失败");
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&gDwrite)))) {
        AppLog::warn("Emoji", "DirectWrite 工厂创建失败");
        return false;
    }
    gInitOk = true;
    return true;
}

uint32_t decodeUtf8(const std::string& text, size_t offset, size_t* length) {
    const unsigned char first = (unsigned char)text[offset];
    uint32_t cp = 0;
    size_t len = 0;
    if (first < 0x80) {
        cp = first;
        len = 1;
    } else if ((first & 0xE0) == 0xC0) {
        cp = first & 0x1F;
        len = 2;
    } else if ((first & 0xF0) == 0xE0) {
        cp = first & 0x0F;
        len = 3;
    } else if ((first & 0xF8) == 0xF0) {
        cp = first & 0x07;
        len = 4;
    } else {
        *length = 0;
        return 0;
    }
    if (offset + len > text.size()) {
        *length = 0;
        return 0;
    }
    for (size_t i = 1; i < len; ++i) {
        const unsigned char next = (unsigned char)text[offset + i];
        if ((next & 0xC0) != 0x80) {
            *length = 0;
            return 0;
        }
        cp = (cp << 6) | (next & 0x3F);
    }
    *length = len;
    return cp;
}

bool isRegionalIndicator(uint32_t cp) { return cp >= 0x1F1E6 && cp <= 0x1F1FF; }
bool isSkinTone(uint32_t cp) { return cp >= 0x1F3FB && cp <= 0x1F3FF; }
bool isModifier(uint32_t cp) {
    return cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x20E3 || isSkinTone(cp) ||
           (cp >= 0xE0020 && cp <= 0xE007F);
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size);
    return out;
}

unsigned int renderTexture(const std::string& token) {
    if (!ensureFactories()) return 0;
    IWICBitmap* bitmap = nullptr;
    ID2D1RenderTarget* target = nullptr;
    IDWriteTextFormat* format = nullptr;
    IDWriteTextLayout* layout = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    unsigned int texture = 0;

    auto cleanup = [&]() {
        if (brush) brush->Release();
        if (layout) layout->Release();
        if (format) format->Release();
        if (target) target->Release();
        if (bitmap) bitmap->Release();
    };

    HRESULT hr = gWic->CreateBitmap(kTextureSize, kTextureSize, GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr) || bitmap == nullptr) {
        cleanup();
        return 0;
    }
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    hr = gD2d->CreateWicBitmapRenderTarget(bitmap, props, &target);
    if (FAILED(hr) || target == nullptr) {
        cleanup();
        return 0;
    }
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    hr = gDwrite->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kFontSize,
                                   L"zh-CN", &format);
    if (SUCCEEDED(hr) && format != nullptr) {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const std::wstring wide = toWide(token);
        hr = gDwrite->CreateTextLayout(wide.c_str(), (UINT32)wide.size(), format,
                                       (float)kTextureSize, (float)kTextureSize, &layout);
        if (SUCCEEDED(hr) && layout != nullptr) {
            target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
            if (brush != nullptr) {
                target->DrawTextLayout(D2D1::Point2F(0, 0), layout, brush,
                                       D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
        }
    }
    hr = target->EndDraw();
    if (FAILED(hr)) {
        cleanup();
        return 0;
    }

    WICRect rect{0, 0, kTextureSize, kTextureSize};
    IWICBitmapLock* lock = nullptr;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockRead, &lock)) || lock == nullptr) {
        cleanup();
        return 0;
    }
    UINT stride = 0, bufferSize = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bufferSize, &data)) ||
        data == nullptr) {
        lock->Release();
        cleanup();
        return 0;
    }
    std::vector<unsigned char> rgba((size_t)kTextureSize * kTextureSize * 4, 0);
    for (int y = 0; y < kTextureSize; ++y) {
        const unsigned char* src = data + (size_t)y * stride;
        unsigned char* dst = rgba.data() + (size_t)y * kTextureSize * 4;
        for (int x = 0; x < kTextureSize; ++x) {
            const unsigned char b = src[x * 4 + 0];
            const unsigned char g = src[x * 4 + 1];
            const unsigned char r = src[x * 4 + 2];
            const unsigned char a = src[x * 4 + 3];
            // D2D 输出预乘 alpha，ImGui 混合需要直通 alpha
            const unsigned int alpha = a;
            dst[x * 4 + 0] = (unsigned char)(alpha ? std::min(255u, (unsigned)r * 255u / alpha) : 0);
            dst[x * 4 + 1] = (unsigned char)(alpha ? std::min(255u, (unsigned)g * 255u / alpha) : 0);
            dst[x * 4 + 2] = (unsigned char)(alpha ? std::min(255u, (unsigned)b * 255u / alpha) : 0);
            dst[x * 4 + 3] = a;
        }
    }
    lock->Release();
    cleanup();

    GLuint glTexture = 0;
    glGenTextures(1, &glTexture);
    glBindTexture(GL_TEXTURE_2D, glTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTextureSize, kTextureSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    texture = glTexture;
    if (texture == 0) AppLog::warn("Emoji", "emoji 纹理创建失败");
    return texture;
}

}  // namespace

bool IsEmojiCodepoint(uint32_t cp) {
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true;
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;   // ☀️❤️✨ 等
    if (cp >= 0x2B00 && cp <= 0x2BFF) return true;   // ⭐⬛ 等
    if (cp == 0x00A9 || cp == 0x00AE || cp == 0x203C || cp == 0x2049 || cp == 0x2122 ||
        cp == 0x2139 || cp == 0x3030 || cp == 0x303D || cp == 0x3297 || cp == 0x3299) {
        return true;
    }
    return false;
}

size_t NextEmojiToken(const std::string& text, size_t offset, std::string* token) {
    if (offset >= text.size()) return 0;
    size_t length = 0;
    const uint32_t first = decodeUtf8(text, offset, &length);
    if (length == 0) return 0;
    bool emoji = IsEmojiCodepoint(first);
    // 数字/#/* + 键帽
    if (!emoji && (first == '#' || first == '*' || (first >= '0' && first <= '9'))) {
        size_t next = offset + length;
        if (next < text.size()) {
            size_t l2 = 0;
            const uint32_t second = decodeUtf8(text, next, &l2);
            if (second == 0x20E3) emoji = true;
        }
    }
    if (!emoji) return 0;

    size_t end = offset + length;
    // 旗帜：两个区域指示符
    if (isRegionalIndicator(first) && end < text.size()) {
        size_t l2 = 0;
        const uint32_t second = decodeUtf8(text, end, &l2);
        if (isRegionalIndicator(second)) end += l2;
    }
    while (end < text.size()) {
        size_t modLen = 0;
        const uint32_t mod = decodeUtf8(text, end, &modLen);
        if (modLen == 0) break;
        if (isModifier(mod)) {
            end += modLen;
            continue;
        }
        // ZWJ 序列：下一个 emoji 基字符
        if (mod == 0x200D) break;  // 已含在 isModifier，理论上不会到这里
        break;
    }
    // 处理 ZWJ 连接链：base + 200D + base + ...
    while (end < text.size()) {
        size_t zwjLen = 0;
        const uint32_t zwj = decodeUtf8(text, end, &zwjLen);
        if (zwj != 0x200D) break;
        size_t nextOffset = end + zwjLen;
        if (nextOffset >= text.size()) break;
        size_t nextLen = 0;
        const uint32_t next = decodeUtf8(text, nextOffset, &nextLen);
        if (nextLen == 0 || !IsEmojiCodepoint(next)) break;
        end = nextOffset + nextLen;
        while (end < text.size()) {
            size_t modLen = 0;
            const uint32_t mod = decodeUtf8(text, end, &modLen);
            if (modLen == 0 || !isModifier(mod)) break;
            end += modLen;
        }
    }
    if (token) *token = text.substr(offset, end - offset);
    return end - offset;
}

unsigned int EmojiTexture(const std::string& token) {
    if (token.empty()) return 0;
    const auto it = gCache.find(token);
    if (it != gCache.end()) return it->second;
    const unsigned int texture = renderTexture(token);
    gCache.emplace(token, texture);
    return texture;
}

}  // namespace Ui
