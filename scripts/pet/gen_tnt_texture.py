"""Generate local-only/grok-private-headers/pet_tnt_texture.h from the Minecraft tnt_side.png.

The source PNG is not redistributable; keep it under local-only. This script
only converts it to the 16x16 RGB565 array used by the embedded TNT renderer.
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "local-only/minecraft-textures/tnt_side.png"
TARGET = ROOT / "local-only/grok-private-headers/pet_tnt_texture.h"


def read_texture(path):
    with Image.open(path) as source:
        if source.size != (16, 16):
            raise ValueError(f"expected a 16x16 texture, got {source.size}")
        image = source.convert("RGB")
    return [((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            for red, green, blue in (image.getpixel((x, y)) for y in range(16) for x in range(16))]


def main():
    values = read_texture(SOURCE)
    lines = ["    " + ", ".join(f"0x{value:04x}" for value in values[index:index + 8]) + ","
             for index in range(0, len(values), 8)]
    header = """// Generated from Minecraft tnt_side.png (16x16) for the personal GrokBot
// experiment. Minecraft assets are copyright Mojang/Microsoft; do not
// redistribute this texture without permission.
#pragma once

#include <cstdint>

namespace pet_tnt_texture {
inline constexpr uint16_t kSide[256] = {
""" + "\n".join(lines) + """
};
}  // namespace pet_tnt_texture
"""
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    TARGET.write_text(header, encoding="utf-8")
    print(f"{TARGET}: 256 texels")


if __name__ == "__main__":
    main()
