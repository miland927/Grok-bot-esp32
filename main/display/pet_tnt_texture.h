#pragma once

#if __has_include("../../local-only/grok-private-headers/pet_tnt_texture.h")
#include "../../local-only/grok-private-headers/pet_tnt_texture.h"
#else
#include <array>
#include <cstdint>

namespace pet_tnt_texture {
inline constexpr auto kSide = [] {
    std::array<uint16_t, 256> pixels{};
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool edge = x == 0 || x == 15 || y == 0 || y == 15;
            const bool stripe = (x + 2 * y) % 8 < 3;
            pixels[y * 16 + x] = edge ? 0x3186 : stripe ? 0xfc22 : 0xba65;
        }
    }
    return pixels;
}();
}

#endif
