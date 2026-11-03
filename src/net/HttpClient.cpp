#include "net/HttpClient.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cwchar>

namespace net {

namespace {

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)size, L'\\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size);
    return out;
}

struct WinHttpHandles {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    ~WinHttpHandles() {
        if (request != nullptr) WinHttpCloseHandle(request);
        if (connection != nullptr) WinHttpCloseHandle(connection);
        if (session != nullptr) WinHttpCloseHandle(session);
    }
};

}  // namespace

std::string urlEncode(const std::string& value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
    }
    return out;
}

bool httpGet(const std::string& url,
             const std::vector<std::pair<std::string, std::string>>& headers,
             HttpResponse* response, std::string* error) {
    if (response == nullptr) return false;
    response->status = 0;
    response->body.clear();

    const std::wstring wideUrl = toWide(url);
    if (wideUrl.empty()) {
        if (error != nullptr) *error = "URL 为空";
        return false;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    components.lpszHostName = host;
    components.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(wchar_t));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(wchar_t));
    if (!WinHttpCrackUrl(wideUrl.c_str(), (DWORD)wideUrl.size(), 0, &components)) {
        if (error != nullptr) *error = "URL 解析失败";
        return false;
    }

    WinHttpHandles handles;
    handles.session = WinHttpOpen(L"bilimusic-cpp/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (handles.session == nullptr) {
        if (error != nullptr) *error = "WinHttpOpen 失败";
        return false;
    }
    WinHttpSetTimeouts(handles.session, 10000, 10000, 20000, 20000);

    handles.connection = WinHttpConnect(handles.session, host, components.nPort, 0);
    if (handles.connection == nullptr) {
        if (error != nullptr) *error = "连接失败";
        return false;
    }

    const DWORD flags = (components.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    handles.request = WinHttpOpenRequest(handles.connection, L"GET", path, nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (handles.request == nullptr) {
        if (error != nullptr) *error = "创建请求失败";
        return false;
    }

    if (!headers.empty()) {
        std::wstring headerBlock;
        for (const auto& [name, value] : headers) {
            if (value.empty()) continue;
            headerBlock += toWide(name);
            headerBlock += L": ";
            headerBlock += toWide(value);
            headerBlock += L"\r\n";
        }
        if (!headerBlock.empty()) {
            WinHttpAddRequestHeaders(handles.request, headerBlock.c_str(),
                                     (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
    }

    if (!WinHttpSendRequest(handles.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (error != nullptr) *error = "发送请求失败（错误码 " + std::to_string(GetLastError()) + "）";
        return false;
    }
    if (!WinHttpReceiveResponse(handles.request, nullptr)) {
        if (error != nullptr) *error = "接收响应失败";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(handles.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    response->status = (int)statusCode;

    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handles.request, &available)) break;
        if (available == 0) break;
        std::string chunk;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(handles.request, chunk.data(), available, &read)) break;
        if (read == 0) break;
        chunk.resize(read);
        response->body += chunk;
        if (response->body.size() > 8u * 1024u * 1024u) break;
    }
    return true;
}

}  // namespace net
