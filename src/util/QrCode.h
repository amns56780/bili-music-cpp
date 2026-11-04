#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace util {

struct QrMatrix {
    int size = 0;                  // 每边模块数
    std::vector<uint8_t> modules;  // size*size，1=黑
    bool valid() const { return size > 0 && (int)modules.size() == size * size; }
};

// 用 qrcodegen（Nayuki，MIT）把文本编码成二维码矩阵
bool makeQrMatrix(const std::string& text, QrMatrix* out);

}  // namespace util
