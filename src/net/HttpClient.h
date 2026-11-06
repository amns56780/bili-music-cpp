#pragma once

#include <string>
#include <vector>

namespace net {

struct HttpResponse {
    int status = 0;
    std::string body;
    // 原始响应头（只收 Set-Cookie，扫码登录要靠它拿 SESSDATA 等 Cookie）
    std::vector<std::pair<std::string, std::string>> setCookies;  // name -> value
};

// 极简同步 HTTP GET（WinHTTP，无第三方依赖）。
// 所有调用都放在后台线程里跑，不要直接在 UI 线程调用。
// maxBytes：响应体上限（字节）。默认 8 MiB 只够 JSON 接口用；
// 下载音频必须传更大的值（Hi-Res 单曲动辄 20–60 MB），否则会被截断成半首歌。
bool httpGet(const std::string& url,
             const std::vector<std::pair<std::string, std::string>>& headers,
             HttpResponse* response, std::string* error,
             size_t maxBytes = 8u * 1024u * 1024u);

std::string urlEncode(const std::string& value);

}  // namespace net
