#include "util/ShareLink.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "miniz.h"
#include "nlohmann/json.hpp"

namespace util {

namespace {

const char* kBase64UrlChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string Base64UrlEncode(const std::vector<unsigned char>& data) {
    std::string out;
    out.reserve((data.size() * 4 + 2) / 3);
    size_t i = 0;
    while (i + 2 < data.size()) {
        const unsigned int triple =
            ((unsigned int)data[i] << 16) | ((unsigned int)data[i + 1] << 8) | data[i + 2];
        out.push_back(kBase64UrlChars[(triple >> 18) & 0x3F]);
        out.push_back(kBase64UrlChars[(triple >> 12) & 0x3F]);
        out.push_back(kBase64UrlChars[(triple >> 6) & 0x3F]);
        out.push_back(kBase64UrlChars[triple & 0x3F]);
        i += 3;
    }
    const size_t rest = data.size() - i;
    if (rest == 1) {
        const unsigned int triple = (unsigned int)data[i] << 16;
        out.push_back(kBase64UrlChars[(triple >> 18) & 0x3F]);
        out.push_back(kBase64UrlChars[(triple >> 12) & 0x3F]);
    } else if (rest == 2) {
        const unsigned int triple = ((unsigned int)data[i] << 16) | ((unsigned int)data[i + 1] << 8);
        out.push_back(kBase64UrlChars[(triple >> 18) & 0x3F]);
        out.push_back(kBase64UrlChars[(triple >> 12) & 0x3F]);
        out.push_back(kBase64UrlChars[(triple >> 6) & 0x3F]);
    }
    return out;
}

int Base64UrlValue(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

bool Base64UrlDecode(const std::string& text, std::vector<unsigned char>* out) {
    out->clear();
    out->reserve(text.size() * 3 / 4 + 3);
    unsigned int buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const int value = Base64UrlValue(c);
        if (value < 0) return false;
        buffer = (buffer << 6) | (unsigned int)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back((unsigned char)((buffer >> bits) & 0xFF));
        }
    }
    return true;
}

bool DeflateRaw(const std::string& input, std::vector<unsigned char>* out) {
    mz_stream stream{};
    if (mz_deflateInit2(&stream, MZ_BEST_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9,
                        MZ_DEFAULT_STRATEGY) != MZ_OK) {
        return false;
    }
    out->clear();
    out->resize(input.size() / 2 + 64);
    stream.next_in = (const unsigned char*)input.data();
    stream.avail_in = (unsigned int)input.size();
    size_t produced = 0;
    for (;;) {
        if (produced == out->size()) out->resize(out->size() * 2 + 64);
        stream.next_out = out->data() + produced;
        stream.avail_out = (unsigned int)(out->size() - produced);
        const int status = mz_deflate(&stream, MZ_FINISH);
        produced = out->size() - stream.avail_out;
        if (status == MZ_STREAM_END) break;
        if (status != MZ_OK && status != MZ_BUF_ERROR) {
            mz_deflateEnd(&stream);
            return false;
        }
    }
    mz_deflateEnd(&stream);
    out->resize(produced);
    return true;
}

bool InflateRaw(const std::vector<unsigned char>& input, std::string* out) {
    mz_stream stream{};
    if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
    out->clear();
    std::vector<unsigned char> chunk(8192);
    stream.next_in = input.data();
    stream.avail_in = (unsigned int)input.size();
    for (;;) {
        stream.next_out = chunk.data();
        stream.avail_out = (unsigned int)chunk.size();
        const int status = mz_inflate(&stream, MZ_NO_FLUSH);
        const size_t produced = chunk.size() - stream.avail_out;
        if (produced > 0) out->append((const char*)chunk.data(), produced);
        if (status == MZ_STREAM_END) break;
        if (status != MZ_OK && status != MZ_BUF_ERROR) {
            mz_inflateEnd(&stream);
            return false;
        }
        if (stream.avail_in == 0 && produced == 0) break;
    }
    mz_inflateEnd(&stream);
    return !out->empty();
}

std::string Trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace((unsigned char)text[begin])) ++begin;
    while (end > begin && std::isspace((unsigned char)text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

}  // namespace

bool IsShareLink(const std::string& text) {
    const std::string trimmed = Trim(text);
    return trimmed.rfind(kShareLinkPrefix, 0) == 0;
}

std::string EncodeShareLink(const std::string& title, const std::vector<std::string>& bvids) {
    nlohmann::json payload;
    payload["v"] = 1;
    payload["t"] = title;
    nlohmann::json list = nlohmann::json::array();
    size_t count = 0;
    for (const std::string& raw : bvids) {
        const std::string bvid = Trim(raw);
        if (bvid.empty()) continue;
        if (++count > kShareLinkMaxSongs) break;
        list.push_back(bvid);
    }
    payload["s"] = std::move(list);
    const std::string json = payload.dump();

    std::vector<unsigned char> compressed;
    if (!DeflateRaw(json, &compressed)) return {};
    return std::string(kShareLinkPrefix) + Base64UrlEncode(compressed);
}

bool DecodeShareLink(const std::string& text, SharedPlaylist* out) {
    if (out == nullptr) return false;
    const std::string trimmed = Trim(text);
    if (trimmed.rfind(kShareLinkPrefix, 0) != 0) return false;
    const std::string body = trimmed.substr(std::strlen(kShareLinkPrefix));

    std::vector<unsigned char> compressed;
    if (!Base64UrlDecode(body, &compressed) || compressed.empty()) return false;
    std::string json;
    if (!InflateRaw(compressed, &json)) return false;

    nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return false;
    SharedPlaylist result;
    result.version = parsed.value("v", 1);
    result.title = parsed.value("t", std::string());
    if (!parsed.contains("s") || !parsed["s"].is_array()) return false;
    for (const auto& item : parsed["s"]) {
        if (!item.is_string()) continue;
        const std::string bvid = Trim(item.get<std::string>());
        if (!bvid.empty()) result.bvids.push_back(bvid);
    }
    if (result.bvids.empty()) return false;
    *out = std::move(result);
    return true;
}

}  // namespace util
