#include "util/QrCode.h"

#include <qrcodegen.hpp>

namespace util {

bool makeQrMatrix(const std::string& text, QrMatrix* out) {
    if (out == nullptr || text.empty()) return false;
    try {
        const qrcodegen::QrCode code =
            qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        out->size = code.getSize();
        out->modules.assign((size_t)out->size * (size_t)out->size, 0);
        for (int y = 0; y < out->size; ++y) {
            for (int x = 0; x < out->size; ++x) {
                out->modules[(size_t)y * (size_t)out->size + (size_t)x] =
                    code.getModule(x, y) ? 1 : 0;
            }
        }
        return true;
    } catch (const std::exception&) {
        out->size = 0;
        out->modules.clear();
        return false;
    }
}

}  // namespace util
