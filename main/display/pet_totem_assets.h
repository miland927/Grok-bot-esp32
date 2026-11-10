#pragma once

#if __has_include("../../local-only/grok-private-headers/pet_totem_assets.h")
#include "../../local-only/grok-private-headers/pet_totem_assets.h"
#else
#include <array>
#include <cstdint>

#include "pet_totem_texture.h"

namespace pet_totem::assets {
inline constexpr int kWidth = 16;
inline constexpr int kHeight = 16;
inline constexpr int kMaskStride = 2;

inline constexpr auto kPixels = [] {
    std::array<uint16_t, kWidth * kHeight> pixels{};
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int dx = x < 8 ? 7 - x : x - 8;
            const int dy = y < 8 ? 7 - y : y - 8;
            const int distance = dx + dy;
            if (distance <= 6) {
                pixels[y * kWidth + x] = distance >= 5 ? 0xff29 : x < y ? 0x4eaf : 0x9ff6;
            }
        }
    }
    return pixels;
}();

inline constexpr auto kMask = [] {
    std::array<uint8_t, kHeight * kMaskStride> mask{};
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (kPixels[y * kWidth + x]) {
                mask[y * kMaskStride + x / 8] |= 1 << (7 - x % 8);
            }
        }
    }
    return mask;
}();

inline constexpr Texture kTexture{kPixels.data(), kMask.data(), kWidth, kHeight, kMaskStride};
}

#endif
