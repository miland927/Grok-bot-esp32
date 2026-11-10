#pragma once

#include <cstdint>

namespace pet_totem {

// RGB565 sprite plus a one-bit alpha mask. The mask stores
// ((width + 7) / 8) bytes per row, MSB is the leftmost pixel.
struct Texture {
    const uint16_t* pixels = nullptr;
    const uint8_t* mask = nullptr;
    int width = 0;
    int height = 0;
    int mask_stride = 0;
};

}  // namespace pet_totem
