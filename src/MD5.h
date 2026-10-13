#pragma once

#include <string>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>

// Standard RFC 1321 MD5 Implementation
namespace md5_detail {

inline void transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        x[i] = (static_cast<uint32_t>(block[j])) |
               (static_cast<uint32_t>(block[j + 1]) << 8) |
               (static_cast<uint32_t>(block[j + 2]) << 16) |
               (static_cast<uint32_t>(block[j + 3]) << 24);
    }

    #define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
    #define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
    #define H(x, y, z) ((x) ^ (y) ^ (z))
    #define I(x, y, z) ((y) ^ ((x) | (~z)))
    #define ROT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
    #define STEP(f, a, b, c, d, x, ac, s) \
        (a += f(b, c, d) + x + (uint32_t)(ac), a = ROT(a, s), a += b)

    // Round 1
    STEP(F, a, b, c, d, x[0],  0xd76aa478, 7);  STEP(F, d, a, b, c, x[1],  0xe8c7b756, 12);
    STEP(F, c, d, a, b, x[2],  0x242070db, 17); STEP(F, b, c, d, a, x[3],  0xc1bdceee, 22);
    STEP(F, a, b, c, d, x[4],  0xf57c0faf, 7);  STEP(F, d, a, b, c, x[5],  0x4787c62a, 12);
    STEP(F, c, d, a, b, x[6],  0xa8304613, 17); STEP(F, b, c, d, a, x[7],  0xfd469501, 22);
    STEP(F, a, b, c, d, x[8],  0x698098d8, 7);  STEP(F, d, a, b, c, x[9],  0x8b44f7af, 12);
    STEP(F, c, d, a, b, x[10], 0xffff5bb1, 17); STEP(F, b, c, d, a, x[11], 0x895cd7be, 22);
    STEP(F, a, b, c, d, x[12], 0x6b901122, 7);  STEP(F, d, a, b, c, x[13], 0xfd987193, 12);
    STEP(F, c, d, a, b, x[14], 0xa679438e, 17); STEP(F, b, c, d, a, x[15], 0x49b40821, 22);

    // Round 2
    STEP(G, a, b, c, d, x[1],  0xf61e2562, 5);  STEP(G, d, a, b, c, x[6],  0xc040b340, 9);
    STEP(G, c, d, a, b, x[11], 0x265e5a51, 14); STEP(G, b, c, d, a, x[0],  0xe9b6c7aa, 20);
    STEP(G, a, b, c, d, x[5],  0xd62f105d, 5);  STEP(G, d, a, b, c, x[10], 0x02441453, 9);
    STEP(G, c, d, a, b, x[15], 0xd8a1e681, 14); STEP(G, b, c, d, a, x[4],  0xe7d3fbc8, 20);
    STEP(G, a, b, c, d, x[9],  0x21e1cde6, 5);  STEP(G, d, a, b, c, x[14], 0xc33707d6, 9);
    STEP(G, c, d, a, b, x[3],  0xf4d50d87, 14); STEP(G, b, c, d, a, x[8],  0x455a14ed, 20);
    STEP(G, a, b, c, d, x[13], 0xa9e3e905, 5);  STEP(G, d, a, b, c, x[2],  0xfcefa3f8, 9);
    STEP(G, c, d, a, b, x[7],  0x676f02d9, 14); STEP(G, b, c, d, a, x[12], 0x8d2a4c8a, 20);

    // Round 3
    STEP(H, a, b, c, d, x[5],  0xfffa3942, 4);  STEP(H, d, a, b, c, x[8],  0x8771f681, 11);
    STEP(H, c, d, a, b, x[11], 0x6d9d6122, 16); STEP(H, b, c, d, a, x[14], 0xfde5380c, 23);
    STEP(H, a, b, c, d, x[1],  0xa4beea44, 4);  STEP(H, d, a, b, c, x[4],  0x4bdecfa9, 11);
    STEP(H, c, d, a, b, x[7],  0xf6bb4b60, 16); STEP(H, b, c, d, a, x[10], 0xbebfbc70, 23);
    STEP(H, a, b, c, d, x[13], 0x289b7ec6, 4);  STEP(H, d, a, b, c, x[0],  0xeaa127fa, 11);
    STEP(H, c, d, a, b, x[3],  0xd4ef3085, 16); STEP(H, b, c, d, a, x[6],  0x04881d05, 23);
    STEP(H, a, b, c, d, x[9],  0xd9d4d039, 4);  STEP(H, d, a, b, c, x[12], 0xe6db99e5, 11);
    STEP(H, c, d, a, b, x[15], 0x1fa27cf8, 16); STEP(H, b, c, d, a, x[2],  0xc4ac5665, 23);

    // Round 4
    STEP(I, a, b, c, d, x[0],  0xf4292244, 6);  STEP(I, d, a, b, c, x[7],  0x432aff97, 10);
    STEP(I, c, d, a, b, x[14], 0xab9423a7, 15); STEP(I, b, c, d, a, x[5],  0xfc93a039, 21);
    STEP(I, a, b, c, d, x[12], 0x655b59c3, 6);  STEP(I, d, a, b, c, x[3],  0x8f0ccc92, 10);
    STEP(I, c, d, a, b, x[10], 0xffeff47d, 15); STEP(I, b, c, d, a, x[1],  0x85845dd1, 21);
    STEP(I, a, b, c, d, x[8],  0x6fa87e4f, 6);  STEP(I, d, a, b, c, x[15], 0xfe2ce6e0, 10);
    STEP(I, c, d, a, b, x[6],  0xa3014314, 15); STEP(I, b, c, d, a, x[13], 0x4e0811a1, 21);
    STEP(I, a, b, c, d, x[4],  0xf7537e82, 6);  STEP(I, d, a, b, c, x[11], 0xbd3af235, 10);
    STEP(I, c, d, a, b, x[2],  0x2ad7d2bb, 15); STEP(I, b, c, d, a, x[9],  0xeb86d391, 21);

    #undef F
    #undef G
    #undef H
    #undef I
    #undef ROT
    #undef STEP

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

} // namespace md5_detail

inline std::string compute_md5_hex(const std::string& input) {
    uint32_t state[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    uint64_t total_bits = input.size() * 8;

    size_t offset = 0;
    while (offset + 64 <= input.size()) {
        md5_detail::transform(state, reinterpret_cast<const uint8_t*>(input.data() + offset));
        offset += 64;
    }

    uint8_t buffer[128] = {0};
    size_t remaining = input.size() - offset;
    if (remaining > 0) {
        std::memcpy(buffer, input.data() + offset, remaining);
    }
    buffer[remaining] = 0x80;

    size_t pad_len = (remaining < 56) ? 64 : 128;
    for (int i = 0; i < 8; ++i) {
        buffer[pad_len - 8 + i] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }

    md5_detail::transform(state, buffer);
    if (pad_len == 128) {
        md5_detail::transform(state, buffer + 64);
    }

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) {
        for (int b = 0; b < 4; ++b) {
            ss << std::setw(2) << static_cast<int>((state[i] >> (b * 8)) & 0xFF);
        }
    }
    return ss.str();
}
