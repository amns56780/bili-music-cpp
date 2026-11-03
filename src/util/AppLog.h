#pragma once

#include <string>

// 极简日志：写入 %USERPROFILE%\.bilimusic\cpp-app.log，同时在控制台版里打到 stderr。
// （和 Java 版共用 ~/.bilimusic 目录，方便对照排查）
namespace AppLog {

void info(const std::string& tag, const std::string& message);
void warn(const std::string& tag, const std::string& message);
void error(const std::string& tag, const std::string& message);

}  // namespace AppLog
