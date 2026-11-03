#include "util/AppLog.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <ctime>

namespace AppLog {

namespace {

std::wstring logFilePath() {
    wchar_t buffer[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer,
                                                 (DWORD)(sizeof(buffer) / sizeof(wchar_t)));
    if (length == 0) return L"cpp-app.log";
    std::wstring dir(buffer, length);
    dir += L"\\.bilimusic";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\cpp-app.log";
}

void write(const char* level, const std::string& tag, const std::string& message) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char timestamp[32];
    std::snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                  local.tm_min, local.tm_sec);

    std::FILE* file = _wfopen(logFilePath().c_str(), L"ab");
    if (file != nullptr) {
        std::fprintf(file, "[%s] [%s] %s: %s\n", timestamp, level, tag.c_str(), message.c_str());
        std::fclose(file);
    }
}

}  // namespace

void info(const std::string& tag, const std::string& message) { write("I", tag, message); }
void warn(const std::string& tag, const std::string& message) { write("W", tag, message); }
void error(const std::string& tag, const std::string& message) { write("E", tag, message); }

}  // namespace AppLog
