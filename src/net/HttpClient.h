#pragma once

#include <string>
#include <vector>

namespace net {

struct HttpResponse {
    int status = 0;
    std::string body;
};

// 极简同步 HTTP GET（WinHTTP，无第三方依赖）。
// 所有调用都放在后台线程里跑，不要直接在 UI 线程调用。
bool httpGet(const std::string& url,
             const std::vector<std::pair<std::string, std::string>>& headers,
             HttpResponse* response, std::string* error);

std::string urlEncode(const std::string& value);

}  // namespace net
