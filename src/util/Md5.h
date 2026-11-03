#pragma once

#include <cstdint>
#include <string>

namespace util {

// 标准 MD5（WBI 的 w_rid 用它），输出 32 位小写十六进制
std::string md5Hex(const std::string& input);

}  // namespace util
