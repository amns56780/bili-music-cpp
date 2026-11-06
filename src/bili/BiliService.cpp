#include "bili/BiliService.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "net/HttpClient.h"
#include "util/AppLog.h"
#include "util/Md5.h"

namespace bili {

namespace {

constexpr const char* kTag = "BiliService";
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/126.0.0.0 Safari/537.36";
constexpr const char* kReferer = "https://www.bilibili.com/";
constexpr const char* kOrigin = "https://www.bilibili.com";

constexpr int kMixinKeyEncTab[64] = {
    46, 47, 18, 2,  53, 8,  23, 32, 15, 50, 10, 31, 58, 3,  45, 35,
    27, 43, 5,  49, 33, 9,  42, 19, 29, 28, 14, 39, 12, 38, 41, 13,
    37, 48, 7,  16, 24, 55, 40, 61, 26, 17, 0,  1,  60, 51, 30, 4,
    22, 25, 54, 21, 56, 59, 6,  63, 57, 62, 11, 36, 20, 34, 44, 52,
};

std::wstring userProfileDir() {
    wchar_t buffer[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer,
                                                 (DWORD)(sizeof(buffer) / sizeof(wchar_t)));
    if (length == 0) return L".";
    return std::wstring(buffer, length);
}

std::wstring settingsPath() { return userProfileDir() + L"\\\\.bilimusic\\settings.json"; }

bool readFileUtf8(const std::wstring& path, std::string* out) {
    std::FILE* file = _wfopen(path.c_str(), L"rb");
    if (file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        return false;
    }
    out->resize((size_t)size);
    const size_t read = size > 0 ? std::fread(out->data(), 1, (size_t)size, file) : 0;
    out->resize(read);
    std::fclose(file);
    return true;
}

bool writeFileUtf8(const std::wstring& path, const std::string& content) {
    std::FILE* file = _wfopen(path.c_str(), L"wb");
    if (file == nullptr) return false;
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
    return true;
}

std::string trim(const std::string& text) {
    const size_t begin = text.find_first_not_of(" \t\\r\\n");
    if (begin == std::string::npos) return {};
    const size_t end = text.find_last_not_of(" \t\\r\\n");
    return text.substr(begin, end - begin + 1);
}

std::string fileKeyOf(const std::string& url) {
    const size_t slash = url.find_last_of('/');
    std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

std::string sanitizeValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c != '!' && c != '\'' && c != '(' && c != ')' && c != '*') out.push_back(c);
    }
    return out;
}

std::string buildQuery(const std::map<std::string, std::string>& params) {
    std::string query;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) query += "&";
        first = false;
        query += key;
        query += "=";
        query += net::urlEncode(value);
    }
    return query;
}

std::string rawQueryForSign(const std::map<std::string, std::string>& params) {
    std::string query;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) query += "&";
        first = false;
        query += key;
        query += "=";
        query += sanitizeValue(value);
    }
    return query;
}

std::vector<std::pair<std::string, std::string>> apiHeaders(const std::string& cookie) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"User-Agent", kUserAgent},
        {"Referer", kReferer},
        {"Origin", kOrigin},
        {"Accept", "application/json, text/plain, */*"},
    };
    if (!cookie.empty()) headers.emplace_back("Cookie", cookie);
    return headers;
}

}  // namespace

int64_t parseDeadlineEpochSeconds(const std::string& url) {
    const std::string marker = "deadline=";
    const size_t pos = url.find(marker);
    if (pos == std::string::npos) return 0;
    size_t end = pos + marker.size();
    while (end < url.size() && url[end] >= '0' && url[end] <= '9') ++end;
    if (end == pos + marker.size()) return 0;
    try {
        return (int64_t)std::stoll(url.substr(pos + marker.size(), end - (pos + marker.size())));
    } catch (const std::exception&) {
        return 0;
    }
}

std::string AudioStreamInfo::qualityLabel() const {
    switch (qualityId) {
        case 30216: return "64K";
        case 30232: return "132K";
        case 30280: return "192K";
        case 30250: return "杜比全景声";
        case 30251: return "Hi-Res 无损";
        default: return "未知音质(" + std::to_string(qualityId) + ")";
    }
}

void BiliService::init() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_) return;
    initialized_ = true;
    loadSettings();

    const auto blob = values_.find("cookie_blob");
    if (blob != values_.end()) {
        try {
            const nlohmann::json cookiesJson = nlohmann::json::parse(blob->second);
            if (cookiesJson.is_object()) {
                for (auto it = cookiesJson.begin(); it != cookiesJson.end(); ++it) {
                    if (it.value().is_string()) cookies_[it.key()] = it.value().get<std::string>();
                }
            }
        } catch (const std::exception&) {
            AppLog::warn(kTag, "settings.json 里的 cookie_blob 解析失败，按未登录处理");
        }
    }
    AppLog::info(kTag, "初始化完成，Cookie 数=" + std::to_string(cookies_.size()));
}

bool BiliService::loadSettings() {
    std::string text;
    if (!readFileUtf8(settingsPath(), &text) || text.empty()) return false;
    try {
        const nlohmann::json root = nlohmann::json::parse(text);
        if (!root.is_object()) return false;
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (it.value().is_string()) values_[it.key()] = it.value().get<std::string>();
        }
        return true;
    } catch (const std::exception& exception) {
        AppLog::warn(kTag, std::string("settings.json 解析失败：") + exception.what());
        return false;
    }
}

bool BiliService::saveSettings() {
    nlohmann::json root = nlohmann::json::object();
    for (const auto& [key, value] : values_) root[key] = value;
    nlohmann::json cookiesJson = nlohmann::json::object();
    for (const auto& [key, value] : cookies_) cookiesJson[key] = value;
    root["cookie_blob"] = cookiesJson.dump();
    return writeFileUtf8(settingsPath(), root.dump(2));
}

bool BiliService::hasCookie() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !cookies_.empty();
}

std::string BiliService::cookieHeader() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string header;
    for (const auto& [key, value] : cookies_) {
        if (!header.empty()) header += "; ";
        header += key;
        header += "=";
        header += value;
    }
    return header;
}

bool BiliService::loggedIn() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return loggedIn_;
}

const std::string& BiliService::userName() const { return userName_; }
long long BiliService::userId() const { return userId_; }

int BiliService::importCookieText(const std::string& text) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::map<std::string, std::string> parsed;
    std::stringstream stream(text);
    std::string segment;
    while (std::getline(stream, segment, ';')) {
        const size_t equals = segment.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(segment.substr(0, equals));
        const std::string value = trim(segment.substr(equals + 1));
        if (!key.empty() && !value.empty()) parsed[key] = value;
    }
    if (parsed.empty()) return 0;
    cookies_ = parsed;
    saveSettings();
    return (int)cookies_.size();
}

std::string BiliService::getSetting(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = values_.find(key);
    return it == values_.end() ? std::string() : it->second;
}

bool BiliService::setSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    values_[key] = value;
    return saveSettings();
}

void BiliService::clearCookie() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    cookies_.clear();
    loggedIn_ = false;
    userName_.clear();
    userId_ = 0;
    saveSettings();
}

void BiliService::applyLoginState(const NavInfo& nav) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    loggedIn_ = nav.loggedIn;
    userName_ = nav.userName;
    userId_ = nav.userId;
}

std::string BiliService::parseBvid(const std::string& input) {
    const std::string lowered = input;
    for (size_t i = 0; i + 12 <= lowered.size(); ++i) {
        if (lowered[i] == 'B' && lowered[i + 1] == 'V') {
            bool valid = true;
            for (size_t j = i + 2; j < i + 12; ++j) {
                const char c = lowered[j];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
                    valid = false;
                    break;
                }
            }
            if (valid) return lowered.substr(i, 12);
        }
    }
    return {};
}

std::string BiliService::ensureMixinKey(bool forceRefresh) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const uint64_t now = GetTickCount64();
    constexpr uint64_t kTtlMs = 24ull * 60ull * 60ull * 1000ull;
    if (!forceRefresh && !mixinKey_.empty() && now - mixinFetchedTick_ < kTtlMs) return mixinKey_;

    const std::string cookie = cookieHeader();
    net::HttpResponse response;
    std::string error;
    if (!net::httpGet("https://api.bilibili.com/x/web-interface/nav", apiHeaders(cookie), &response, &error)) {
        AppLog::warn(kTag, "获取 nav 失败：" + error);
        return mixinKey_;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(response.body);
        const nlohmann::json data = root.value("data", nlohmann::json::object());
        const nlohmann::json wbiImg = data.value("wbi_img", nlohmann::json::object());
        const std::string imgUrl = wbiImg.value("img_url", std::string());
        const std::string subUrl = wbiImg.value("sub_url", std::string());
        const std::string raw = fileKeyOf(imgUrl) + fileKeyOf(subUrl);
        if (raw.size() >= 64) {
            std::string permuted;
            permuted.reserve(64);
            for (int index : kMixinKeyEncTab) permuted.push_back(raw[(size_t)index]);
            mixinKey_ = permuted.substr(0, 32);
            mixinFetchedTick_ = now;
            AppLog::info(kTag, "WBI mixin_key 已刷新：" + mixinKey_);
        }
    } catch (const std::exception& exception) {
        AppLog::warn(kTag, std::string("解析 nav 失败：") + exception.what());
    }
    return mixinKey_;
}

std::map<std::string, std::string> BiliService::signParams(
    const std::map<std::string, std::string>& params, const std::string& mixinKey) const {
    std::map<std::string, std::string> signedParams = params;
    signedParams["wts"] = std::to_string((long long)time(nullptr));
    if (mixinKey.empty()) return signedParams;
    const std::string query = rawQueryForSign(signedParams);
    signedParams["w_rid"] = util::md5Hex(query + mixinKey);
    return signedParams;
}

NavInfo BiliService::fetchNav() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    NavInfo info;
    const std::string cookie = cookieHeader();
    net::HttpResponse response;
    std::string error;
    if (!net::httpGet("https://api.bilibili.com/x/web-interface/nav", apiHeaders(cookie), &response, &error)) {
        info.message = error;
        return info;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(response.body);
        const nlohmann::json data = root.value("data", nlohmann::json::object());
        info.ok = true;
        info.loggedIn = data.value("isLogin", false);
        info.userName = data.value("uname", std::string());
        info.userId = data.value("mid", (long long)0);
        const nlohmann::json wbiImg = data.value("wbi_img", nlohmann::json::object());
        const std::string imgUrl = wbiImg.value("img_url", std::string());
        const std::string subUrl = wbiImg.value("sub_url", std::string());
        const std::string raw = fileKeyOf(imgUrl) + fileKeyOf(subUrl);
        if (raw.size() >= 64) {
            std::string permuted;
            for (int index : kMixinKeyEncTab) permuted.push_back(raw[(size_t)index]);
            mixinKey_ = permuted.substr(0, 32);
            mixinFetchedTick_ = GetTickCount64();
        }
        if (info.loggedIn) {
            loggedIn_ = true;
            userName_ = info.userName;
            userId_ = info.userId;
        } else {
            loggedIn_ = false;
            userName_.clear();
            userId_ = 0;
        }
        AppLog::info(kTag, "nav: loggedIn=" + std::string(info.loggedIn ? "true" : "false") +
                             " uname=" + info.userName);
    } catch (const std::exception& exception) {
        info.message = std::string("nav 解析失败：") + exception.what();
    }
    return info;
}

VideoInfo BiliService::fetchVideo(const std::string& bvid) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    VideoInfo info;
    net::HttpResponse response;
    std::string error;
    const std::string url = "https://api.bilibili.com/x/web-interface/view?bvid=" + bvid;
    if (!net::httpGet(url, apiHeaders(cookieHeader()), &response, &error)) {
        info.message = error;
        return info;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(response.body);
        const int code = root.value("code", -1);
        if (code != 0) {
            info.message = std::string("view 接口返回 code=") + std::to_string(code) + " " +
                           root.value("message", std::string());
            return info;
        }
        const nlohmann::json data = root.value("data", nlohmann::json::object());
        info.ok = true;
        info.bvid = data.value("bvid", bvid);
        info.cid = data.value("cid", (long long)0);
        info.title = data.value("title", std::string());
        const nlohmann::json owner = data.value("owner", nlohmann::json::object());
        info.artist = owner.value("name", std::string());
        info.coverUrl = data.value("pic", std::string());
        info.durationMs = (long long)data.value("duration", 0) * 1000;
    } catch (const std::exception& exception) {
        info.message = std::string("view 解析失败：") + exception.what();
    }
    return info;
}

AudioStreamInfo BiliService::resolveAudio(const std::string& bvid, long long cid, int qualityId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    AudioStreamInfo info;

    std::map<std::string, std::string> params = {
        {"bvid", bvid}, {"cid", std::to_string(cid)}, {"fnval", "4048"},
        {"fnver", "0"}, {"fourk", "1"}, {"qn", "0"},
    };
    auto tryFetch = [&](const std::string& baseUrl, const std::map<std::string, std::string>& queryParams) {
        const std::string url = baseUrl + buildQuery(queryParams);
        net::HttpResponse response;
        std::string error;
        if (!net::httpGet(url, apiHeaders(cookieHeader()), &response, &error)) {
            info.message = error;
            return false;
        }
        try {
            const nlohmann::json root = nlohmann::json::parse(response.body);
            const int code = root.value("code", -1);
            if (code != 0) {
                info.message = std::string("playurl 返回 code=") + std::to_string(code) + " " +
                               root.value("message", std::string());
                return false;
            }
            const nlohmann::json data = root.value("data", nlohmann::json::object());
            const nlohmann::json dash = data.value("dash", nlohmann::json::object());
            const nlohmann::json audio = dash.value("audio", nlohmann::json::array());

            struct Candidate {
                int id = 0;
                std::string baseUrl;
                std::vector<std::string> backups;
                long long bandwidth = 0;
            };
            std::vector<Candidate> candidates;
            for (const auto& item : audio) {
                Candidate candidate;
                candidate.id = item.value("id", 0);
                candidate.baseUrl = item.value("baseUrl", std::string());
                if (candidate.baseUrl.empty()) candidate.baseUrl = item.value("base_url", std::string());
                if (item.contains("backupUrl")) {
                    const auto& backups = item["backupUrl"];
                    if (backups.is_array()) {
                        for (const auto& backup : backups) {
                            if (backup.is_string()) candidate.backups.push_back(backup.get<std::string>());
                        }
                    }
                }
                candidate.bandwidth = item.value("bandwidth", (long long)0);
                if (!candidate.baseUrl.empty()) candidates.push_back(std::move(candidate));
            }

            std::sort(candidates.begin(), candidates.end(),
                      [](const Candidate& left, const Candidate& right) {
                          return left.bandwidth > right.bandwidth;
                      });

            const Candidate* picked = nullptr;
            if (qualityId > 0) {
                for (const auto& candidate : candidates) {
                    if (candidate.id == qualityId) { picked = &candidate; break; }
                }
            }
            if (picked == nullptr) {
                const int autoChain[3] = {30280, 30232, 30216};
                for (int wanted : autoChain) {
                    for (const auto& candidate : candidates) {
                        if (candidate.id == wanted) { picked = &candidate; break; }
                    }
                    if (picked != nullptr) break;
                }
            }
            if (picked == nullptr && !candidates.empty()) picked = &candidates.front();

            if (picked != nullptr) {
                info.ok = true;
                info.url = picked->baseUrl;
                info.backupUrls = picked->backups;
                info.deadlineEpochSeconds = parseDeadlineEpochSeconds(picked->baseUrl);
                info.qualityId = picked->id;
                info.downgraded = qualityId > 0 && qualityId != picked->id;
                info.message = "取流成功";
                return true;
            }

            // dash 为空时用 durl 兜底
            const nlohmann::json durl = data.value("durl", nlohmann::json::array());
            for (const auto& item : durl) {
                const std::string url = item.value("url", std::string());
                if (!url.empty()) {
                    info.ok = true;
                    info.url = url;
                    info.deadlineEpochSeconds = parseDeadlineEpochSeconds(url);
                    info.qualityId = data.value("quality", 0);
                    info.message = "取流成功（durl）";
                    return true;
                }
            }
            info.message = "playurl 没有返回音频流";
            return false;
        } catch (const std::exception& exception) {
            info.message = std::string("playurl 解析失败：") + exception.what();
            return false;
        }
    };

    if (!mixinKey_.empty() || !ensureMixinKey(false).empty()) {
        if (tryFetch("https://api.bilibili.com/x/player/wbi/playurl?",
                     signParams(params, ensureMixinKey(false)))) {
            return info;
        }
        AppLog::warn(kTag, "wbi/playurl 失败，强刷 WBI key 后重试：" + info.message);
        if (tryFetch("https://api.bilibili.com/x/player/wbi/playurl?",
                     signParams(params, ensureMixinKey(true)))) {
            return info;
        }
    }

    AppLog::warn(kTag, "wbi 取流失败，改用备选接口：" + info.message);
    tryFetch("https://api.bilibili.com/x/player/playurl?", params);
    return info;
}

namespace {

// 登录成功时 data.url 里也带着 Cookie（crossDomain URL），Set-Cookie 拿不到时兜底用
void appendCookiesFromUrl(const std::string& url, std::map<std::string, std::string>* cookies) {
    const size_t question = url.find('?');
    if (question == std::string::npos) return;
    std::string query = url.substr(question + 1);
    size_t start = 0;
    while (start < query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();
        const std::string pair = query.substr(start, end - start);
        const size_t equals = pair.find('=');
        if (equals != std::string::npos) {
            const std::string name = pair.substr(0, equals);
            std::string value = pair.substr(equals + 1);
            // URL 里是百分号编码的，这里还原成原始 Cookie 值
            std::string decoded;
            decoded.reserve(value.size());
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '%' && i + 2 < value.size()) {
                    auto hexValue = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    const int high = hexValue(value[i + 1]);
                    const int low = hexValue(value[i + 2]);
                    if (high >= 0 && low >= 0) {
                        decoded.push_back((char)((high << 4) | low));
                        i += 2;
                        continue;
                    }
                }
                decoded.push_back(value[i]);
            }
            if (!name.empty() && !decoded.empty()) (*cookies)[name] = decoded;
        }
        start = end + 1;
    }
}

}  // namespace

QrCodeInfo BiliService::qrGenerate() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    QrCodeInfo info;
    net::HttpResponse response;
    std::string error;
    if (!net::httpGet("https://passport.bilibili.com/x/passport-login/web/qrcode/generate",
                      apiHeaders(std::string()), &response, &error)) {
        info.message = error;
        return info;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(response.body);
        const int code = root.value("code", -1);
        if (code != 0) {
            info.message = std::string("二维码接口返回 code=") + std::to_string(code) + " " +
                           root.value("message", std::string());
            return info;
        }
        const nlohmann::json data = root.value("data", nlohmann::json::object());
        info.url = data.value("url", std::string());
        info.key = data.value("qrcode_key", std::string());
        info.ok = !info.url.empty() && !info.key.empty();
        if (!info.ok) info.message = "二维码接口没有返回 url / qrcode_key";
    } catch (const std::exception& exception) {
        info.message = std::string("二维码解析失败：") + exception.what();
    }
    return info;
}

QrPollInfo BiliService::qrPoll(const std::string& qrcodeKey) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    QrPollInfo info;
    const std::string url =
        "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" +
        net::urlEncode(qrcodeKey);
    net::HttpResponse response;
    std::string error;
    if (!net::httpGet(url, apiHeaders(std::string()), &response, &error)) {
        info.message = error;
        return info;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(response.body);
        const int rootCode = root.value("code", -1);
        const nlohmann::json data = root.value("data", nlohmann::json::object());
        const int code = data.value("code", -1);
        info.ok = rootCode == 0;
        if (!info.ok) {
            info.message = std::string("扫码轮询失败 code=") + std::to_string(rootCode) + " " +
                           root.value("message", std::string());
            return info;
        }
        info.message = data.value("message", std::string());
        if (code == 0) {
            info.success = true;
            for (const auto& [name, value] : response.setCookies) info.cookies[name] = value;
            appendCookiesFromUrl(data.value("url", std::string()), &info.cookies);
        } else if (code == 86090) {
            info.scanned = true;
        } else if (code == 86038) {
            info.expired = true;
        }
    } catch (const std::exception& exception) {
        info.message = std::string("扫码轮询解析失败：") + exception.what();
    }
    return info;
}

void BiliService::importCookies(const std::map<std::string, std::string>& cookies) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& [name, value] : cookies) {
        if (!name.empty() && !value.empty()) cookies_[name] = value;
    }
    saveSettings();
    AppLog::info(kTag, "扫码登录 Cookie 已写入，共 " + std::to_string(cookies_.size()) + " 条");
}

namespace {

std::string joinUrl(const char* base,
                    const std::map<std::string, std::string>& params) {
    return std::string(base) + buildQuery(params);
}

}  // namespace

std::vector<FavFolder> BiliService::favFolders(long long mid, std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<FavFolder> folders;
    std::map<std::string, std::string> params = {{"up_mid", std::to_string(mid)}};
    const auto signedParams = signParams(params, ensureMixinKey(false));
    net::HttpResponse response;
    std::string httpError;
    if (!net::httpGet(joinUrl("https://api.bilibili.com/x/v3/fav/folder/created/list-all?",
                             signedParams),
                      apiHeaders(cookieHeader()), &response, &httpError)) {
        if (error) *error = httpError;
        return folders;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(response.body);
        const int code = root.value("code", -1);
        if (code != 0) {
            if (error) {
                *error = std::string("收藏夹列表返回 code=") + std::to_string(code) + " " +
                         root.value("message", std::string());
            }
            return folders;
        }
        const nlohmann::json data = root.value("data", nlohmann::json::object());
        for (const auto& item : data.value("list", nlohmann::json::array())) {
            FavFolder folder;
            folder.id = item.value("id", (int64_t)0);
            folder.title = item.value("title", std::string());
            folder.mediaCount = item.value("media_count", 0);
            if (folder.id != 0 && !folder.title.empty()) folders.push_back(std::move(folder));
        }
    } catch (const std::exception& exception) {
        if (error) *error = std::string("收藏夹列表解析失败：") + exception.what();
    }
    AppLog::info(kTag, "收藏夹列表：" + std::to_string(folders.size()) + " 个");
    return folders;
}

ImportedSongsResult BiliService::importFavFolder(long long mediaId,
                                                const ImportProgressFn& progress) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ImportedSongsResult result;
    constexpr int kPageSize = 20;
    constexpr int kMaxPages = 50;
    std::string folderTitle;
    int page = 1;
    bool hasMore = true;
    while (hasMore && page <= kMaxPages) {
        std::map<std::string, std::string> params = {
            {"media_id", std::to_string(mediaId)},
            {"pn", std::to_string(page)},
            {"ps", std::to_string(kPageSize)},
            {"platform", "web"},
        };
        net::HttpResponse response;
        std::string httpError;
        if (!net::httpGet(joinUrl("https://api.bilibili.com/x/v3/fav/resource/list?",
                                 signParams(params, ensureMixinKey(false))),
                          apiHeaders(cookieHeader()), &response, &httpError)) {
            result.message = httpError;
            return result;
        }
        try {
            const nlohmann::json root = nlohmann::json::parse(response.body);
            const int code = root.value("code", -1);
            if (code != 0) {
                result.message = std::string("收藏夹内容返回 code=") + std::to_string(code) + " " +
                                 root.value("message", std::string());
                return result;
            }
            const nlohmann::json data = root.value("data", nlohmann::json::object());
            const nlohmann::json info = data.value("info", nlohmann::json::object());
            if (folderTitle.empty()) folderTitle = info.value("title", std::string());
            result.total = info.value("media_count", result.total);
            for (const auto& media : data.value("medias", nlohmann::json::array())) {
                ImportedSong song;
                song.bvid = media.value("bvid", std::string());
                if (song.bvid.empty()) continue;
                song.title = media.value("title", std::string());
                song.coverUrl = media.value("cover", std::string());
                song.durationMs = (int64_t)media.value("duration", 0) * 1000;
                const nlohmann::json upper = media.value("upper", nlohmann::json::object());
                song.artist = upper.value("name", std::string());
                result.songs.push_back(std::move(song));
            }
            hasMore = data.value("has_more", false);
            if (progress) {
                progress("正在导入「" + folderTitle + "」：已读取 " +
                             std::to_string(result.songs.size()) + " 首",
                         (int)result.songs.size(),
                         result.total > 0 ? result.total : (int)result.songs.size());
            }
            ++page;
        } catch (const std::exception& exception) {
            result.message = std::string("收藏夹内容解析失败：") + exception.what();
            return result;
        }
    }
    resolveCids(&result.songs, progress);
    result.ok = true;
    result.message = "取到 " + std::to_string(result.songs.size()) + " 首";
    return result;
}

ImportedSongsResult BiliService::importToView(const ImportProgressFn& progress) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ImportedSongsResult result;
    std::map<std::string, std::string> params = {
        {"access_key", ""}, {"pn", "1"}, {"ps", "50"}};
    net::HttpResponse response;
    std::string httpError;
    if (!net::httpGet(joinUrl("https://api.bilibili.com/x/v2/history/toview?",
                             signParams(params, ensureMixinKey(false))),
                      apiHeaders(cookieHeader()), &response, &httpError)) {
        result.message = httpError;
        return result;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(response.body);
        const int code = root.value("code", -1);
        if (code != 0) {
            result.message = std::string("稍后再看返回 code=") + std::to_string(code) + " " +
                             root.value("message", std::string());
            return result;
        }
        const nlohmann::json data = root.value("data", nlohmann::json::object());
        for (const auto& item : data.value("list", nlohmann::json::array())) {
            ImportedSong song;
            song.bvid = item.value("bvid", std::string());
            song.cid = item.value("cid", (int64_t)0);
            if (song.bvid.empty() || song.cid <= 0) continue;
            song.title = item.value("title", std::string());
            song.coverUrl = item.value("pic", std::string());
            song.durationMs = (int64_t)item.value("duration", 0) * 1000;
            const nlohmann::json owner = item.value("owner", nlohmann::json::object());
            song.artist = owner.value("name", std::string());
            result.songs.push_back(std::move(song));
        }
        if (progress) {
            progress("正在导入「稍后再看」：已读取 " + std::to_string(result.songs.size()) + " 首",
                     (int)result.songs.size(), (int)result.songs.size());
        }
    } catch (const std::exception& exception) {
        result.message = std::string("稍后再看解析失败：") + exception.what();
        return result;
    }
    result.total = (int)result.songs.size();
    result.ok = true;
    result.message = "取到 " + std::to_string(result.songs.size()) + " 首";
    return result;
}

void BiliService::resolveCids(std::vector<ImportedSong>* songs, const ImportProgressFn& progress) {
    std::vector<ImportedSong> resolved;
    resolved.reserve(songs->size());
    int index = 0;
    for (ImportedSong& song : *songs) {
        if (song.cid > 0) {
            resolved.push_back(std::move(song));
            continue;
        }
        ++index;
        if (progress) {
            progress("正在补齐音频信息：" + std::to_string(index) + "/" + std::to_string(songs->size()),
                     index, (int)songs->size());
        }
        std::map<std::string, std::string> params = {{"bvid", song.bvid}};
        net::HttpResponse response;
        std::string httpError;
        if (!net::httpGet(joinUrl("https://api.bilibili.com/x/web-interface/view?",
                                 signParams(params, ensureMixinKey(false))),
                          apiHeaders(cookieHeader()), &response, &httpError)) {
            continue;
        }
        try {
            const nlohmann::json root = nlohmann::json::parse(response.body);
            if (root.value("code", -1) != 0) continue;
            const nlohmann::json data = root.value("data", nlohmann::json::object());
            song.cid = data.value("cid", (int64_t)0);
            if (song.cid <= 0) continue;
            if (song.title.empty()) song.title = data.value("title", std::string());
            if (song.artist.empty()) {
                const nlohmann::json owner = data.value("owner", nlohmann::json::object());
                song.artist = owner.value("name", std::string());
            }
            if (song.coverUrl.empty()) song.coverUrl = data.value("pic", std::string());
            if (song.durationMs <= 0) {
                song.durationMs = (int64_t)data.value("duration", 0) * 1000;
            }
            resolved.push_back(std::move(song));
        } catch (const std::exception&) {
            continue;
        }
    }
    *songs = std::move(resolved);
}

namespace {

int64_t parseDurationText(const std::string& text) {
    if (text.empty()) return 0;
    int64_t total = 0;
    int64_t current = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
        } else if (c == ':') {
            total = total * 60 + current;
            current = 0;
        }
    }
    total = total * 60 + current;
    return total * 1000;
}

}  // namespace

ImportedSongsResult BiliService::importSpaceUploads(long long mid,
                                                   const ImportProgressFn& progress) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ImportedSongsResult result;
    // 先取 UP 主昵称（拿不到就用「UP主 mid」）
    {
        net::HttpResponse response;
        std::string httpError;
        const std::string url =
            "https://api.bilibili.com/x/web-interface/card?mid=" + std::to_string(mid);
        if (net::httpGet(url, apiHeaders(cookieHeader()), &response, &httpError)) {
            try {
                const nlohmann::json root = nlohmann::json::parse(response.body);
                if (root.value("code", -1) == 0) {
                    const nlohmann::json data = root.value("data", nlohmann::json::object());
                    const nlohmann::json card = data.value("card", nlohmann::json::object());
                    result.suggestedTitle = card.value("name", std::string());
                }
            } catch (const std::exception&) {
            }
        }
    }
    if (result.suggestedTitle.empty()) {
        result.suggestedTitle = "UP主 " + std::to_string(mid);
    }
    const std::string name = result.suggestedTitle;

    int page = 1;
    constexpr int kMaxPages = 50;
    while (page <= kMaxPages) {
        std::map<std::string, std::string> params = {
            {"mid", std::to_string(mid)},
            {"pn", std::to_string(page)},
            {"ps", "30"},
            {"order", "pubdate"},
            {"platform", "web"},
            {"web_location", "1550101"},
        };
        net::HttpResponse response;
        std::string httpError;
        if (!net::httpGet(joinUrl("https://api.bilibili.com/x/space/wbi/arc/search?",
                                 signParams(params, ensureMixinKey(false))),
                          apiHeaders(cookieHeader()), &response, &httpError)) {
            result.message = httpError;
            return result;
        }
        try {
            const nlohmann::json root = nlohmann::json::parse(response.body);
            const int code = root.value("code", -1);
            if (code != 0) {
                result.message = std::string("UP主投稿返回 code=") + std::to_string(code) + " " +
                                 root.value("message", std::string());
                return result;
            }
            const nlohmann::json data = root.value("data", nlohmann::json::object());
            const nlohmann::json pageInfo = data.value("page", nlohmann::json::object());
            result.total = pageInfo.value("count", result.total);
            const nlohmann::json list = data.value("list", nlohmann::json::object());
            const nlohmann::json vlist = list.value("vlist", nlohmann::json::array());
            if (vlist.empty()) break;
            for (const auto& item : vlist) {
                ImportedSong song;
                song.bvid = item.value("bvid", std::string());
                if (song.bvid.empty()) continue;
                song.title = item.value("title", std::string());
                song.coverUrl = item.value("pic", std::string());
                song.artist = name;
                song.durationMs = parseDurationText(item.value("length", std::string()));
                result.songs.push_back(std::move(song));
            }
            if (progress) {
                progress("正在导入「" + name + "」的投稿：已读取 " +
                             std::to_string(result.songs.size()) + " 首",
                         (int)result.songs.size(),
                         result.total > 0 ? result.total : (int)result.songs.size());
            }
            ++page;
        } catch (const std::exception& exception) {
            result.message = std::string("UP主投稿解析失败：") + exception.what();
            return result;
        }
    }
    resolveCids(&result.songs, progress);
    result.ok = true;
    result.message = "取到 " + std::to_string(result.songs.size()) + " 首";
    return result;
}

ImportedSongsResult BiliService::importSeason(long long seasonId, const ImportProgressFn& progress) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ImportedSongsResult result;
    int page = 1;
    constexpr int kMaxPages = 50;
    while (page <= kMaxPages) {
        std::map<std::string, std::string> params = {
            {"season_id", std::to_string(seasonId)},
            {"page_num", std::to_string(page)},
            {"page_size", "30"},
        };
        net::HttpResponse response;
        std::string httpError;
        if (!net::httpGet(joinUrl("https://api.bilibili.com/x/polymer/web-space/seasons_archives_list?",
                                 signParams(params, ensureMixinKey(false))),
                          apiHeaders(cookieHeader()), &response, &httpError)) {
            result.message = httpError;
            return result;
        }
        try {
            const nlohmann::json root = nlohmann::json::parse(response.body);
            const int code = root.value("code", -1);
            if (code != 0) {
                result.message = std::string("合集接口返回 code=") + std::to_string(code) + " " +
                                 root.value("message", std::string());
                return result;
            }
            const nlohmann::json data = root.value("data", nlohmann::json::object());
            const nlohmann::json meta = data.value("meta", nlohmann::json::object());
            if (result.suggestedTitle.empty()) {
                result.suggestedTitle = meta.value("name", std::string());
            }
            const nlohmann::json pageInfo = data.value("page", nlohmann::json::object());
            result.total = pageInfo.value("total", result.total);
            const nlohmann::json archives = data.value("archives", nlohmann::json::array());
            if (archives.empty()) break;
            for (const auto& item : archives) {
                ImportedSong song;
                song.bvid = item.value("bvid", std::string());
                // 合集接口不返回 cid，先占位，循环结束后统一用 view 接口补齐
                song.cid = item.value("cid", (int64_t)0);
                if (song.bvid.empty()) continue;
                song.title = item.value("title", std::string());
                song.coverUrl = item.value("pic", std::string());
                song.durationMs = (int64_t)item.value("duration", 0) * 1000;
                song.artist = result.suggestedTitle;
                result.songs.push_back(std::move(song));
            }
            if (progress) {
                progress("正在导入合集「" + result.suggestedTitle + "」：" +
                             std::to_string(result.songs.size()) + " 首",
                         (int)result.songs.size(),
                         result.total > 0 ? result.total : (int)result.songs.size());
            }
            ++page;
        } catch (const std::exception& exception) {
            result.message = std::string("合集解析失败：") + exception.what();
            return result;
        }
    }
    if (result.suggestedTitle.empty()) result.suggestedTitle = "合集 " + std::to_string(seasonId);
    resolveCids(&result.songs, progress);
    result.ok = true;
    result.message = "取到 " + std::to_string(result.songs.size()) + " 首";
    return result;
}

}  // namespace bili
