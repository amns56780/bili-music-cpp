#include "util/Md5.h"

#include <array>
#include <cstring>

namespace util {

namespace {

struct Md5Context {
    uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    uint64_t bitCount = 0;
    unsigned char buffer[64] = {};
    size_t bufferSize = 0;
};

constexpr uint32_t kShift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

constexpr uint32_t kTable[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u,
    0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u,
    0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du,
    0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
    0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
    0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u,
    0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu,
    0xeb86d391u,
};

inline uint32_t rotateLeft(uint32_t value, uint32_t shift) {
    return (value << shift) | (value >> (32u - shift));
}

void processBlock(Md5Context& context, const unsigned char* block) {
    uint32_t words[16];
    for (int i = 0; i < 16; ++i) {
        words[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
                   ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
    }
    uint32_t a = context.state[0];
    uint32_t b = context.state[1];
    uint32_t c = context.state[2];
    uint32_t d = context.state[3];
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t f = 0;
        uint32_t g = 0;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const uint32_t temp = d;
        d = c;
        c = b;
        b = b + rotateLeft(a + f + kTable[i] + words[g], kShift[i]);
        a = temp;
    }
    context.state[0] += a;
    context.state[1] += b;
    context.state[2] += c;
    context.state[3] += d;
}

void update(Md5Context& context, const void* data, size_t size) {
    const auto* bytes = (const unsigned char*)data;
    context.bitCount += (uint64_t)size * 8;
    size_t offset = 0;
    if (context.bufferSize > 0) {
        const size_t need = 64 - context.bufferSize;
        const size_t take = size < need ? size : need;
        std::memcpy(context.buffer + context.bufferSize, bytes, take);
        context.bufferSize += take;
        offset += take;
        if (context.bufferSize == 64) {
            processBlock(context, context.buffer);
            context.bufferSize = 0;
        }
    }
    while (offset + 64 <= size) {
        processBlock(context, bytes + offset);
        offset += 64;
    }
    if (offset < size) {
        std::memcpy(context.buffer, bytes + offset, size - offset);
        context.bufferSize = size - offset;
    }
}

std::array<unsigned char, 16> finalize(Md5Context& context) {
    const uint64_t bitCount = context.bitCount;
    unsigned char padding[72] = {};
    padding[0] = 0x80;
    const size_t padSize = (context.bufferSize < 56) ? (56 - context.bufferSize) : (120 - context.bufferSize);
    update(context, padding, padSize);
    unsigned char lengthBytes[8];
    for (int i = 0; i < 8; ++i) lengthBytes[i] = (unsigned char)((bitCount >> (8 * i)) & 0xff);
    // update 会改 bitCount，这里长度已经单独保存
    update(context, lengthBytes, 8);
    std::array<unsigned char, 16> digest{};
    for (int i = 0; i < 4; ++i) {
        digest[i * 4] = (unsigned char)(context.state[i] & 0xff);
        digest[i * 4 + 1] = (unsigned char)((context.state[i] >> 8) & 0xff);
        digest[i * 4 + 2] = (unsigned char)((context.state[i] >> 16) & 0xff);
        digest[i * 4 + 3] = (unsigned char)((context.state[i] >> 24) & 0xff);
    }
    return digest;
}

}  // namespace

std::string md5Hex(const std::string& input) {
    Md5Context context;
    update(context, input.data(), input.size());
    const std::array<unsigned char, 16> digest = finalize(context);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (unsigned char byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

}  // namespace util
