"""Pack the totem texture and soundtrack into /grokbot/totem/totem.gra.

The pack format is deliberately tiny and read-only:
    0  "TTA1"
    4  u16 width
    6  u16 height
    8  u32 audio offset
   12  u32 crc32 of bytes 16..end
   16  width*height RGB565 little-endian pixels
       (width+7)/8 * height one-bit mask, MSB leftmost
       Ogg Opus soundtrack

Usage:
    python scripts/pet/pack_totem_pack.py
"""

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TEXTURE = ROOT / "local-only" / "totem-art" / "totem-source.png"
DEFAULT_AUDIO = ROOT / "local-only" / "grok_totem_sound.ogg"
DEFAULT_STAGE = ROOT / "local-only" / "sd-staging" / "grokbot" / "totem"


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def build(texture_path, audio_path):
    image = Image.open(texture_path).convert("RGBA")
    width, height = image.size
    if not (8 <= width <= 64 and 8 <= height <= 64):
        raise SystemExit(f"texture must be 8..64 px square-ish, got {width}x{height}")
    stride = (width + 7) // 8
    pixels = bytearray()
    mask = bytearray(stride * height)
    for y in range(height):
        for x in range(width):
            r, g, b, a = image.getpixel((x, y))
            value = rgb565((r, g, b)) if a >= 128 else 0
            pixels += struct.pack("<H", value)
            if a >= 128:
                mask[y * stride + x // 8] |= 1 << (7 - x % 8)
    audio = audio_path.read_bytes()
    if not audio.startswith(b"OggS"):
        raise SystemExit(f"not an Ogg file: {audio_path}")
    if len(audio) > 48 * 1024:
        raise SystemExit(f"audio too large: {len(audio)} bytes")

    payload = bytes(pixels) + bytes(mask) + audio
    audio_offset = 16 + len(pixels) + len(mask)
    header = b"TTA1" + struct.pack("<HHII", width, height, audio_offset,
                                    zlib.crc32(payload) & 0xFFFFFFFF)
    pack = header + payload
    if len(pack) > 96 * 1024:
        raise SystemExit(f"pack too large: {len(pack)} bytes")
    return pack


def validate(pack):
    if len(pack) <= 16 or not pack.startswith(b"TTA1"):
        raise SystemExit("bad magic")
    width, height, audio_offset, crc = struct.unpack("<HHII", pack[4:16])
    if not (8 <= width <= 64 and 8 <= height <= 64):
        raise SystemExit("bad size")
    stride = (width + 7) // 8
    expected = 16 + width * height * 2 + stride * height
    if audio_offset != expected or audio_offset >= len(pack):
        raise SystemExit(f"bad audio offset {audio_offset} != {expected}")
    if len(pack) - audio_offset > 48 * 1024:
        raise SystemExit("audio too large")
    if zlib.crc32(pack[16:]) & 0xFFFFFFFF != crc:
        raise SystemExit("bad crc")
    if not pack[audio_offset:audio_offset + 4] == b"OggS":
        raise SystemExit("audio is not Ogg")
    return width, height, audio_offset


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--texture", type=Path, default=DEFAULT_TEXTURE)
    parser.add_argument("--audio", type=Path, default=DEFAULT_AUDIO)
    parser.add_argument("--out", type=Path, default=DEFAULT_STAGE)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    pack = build(args.texture, args.audio)
    target = args.out / "totem.gra"
    target.write_bytes(pack)
    width, height, audio_offset = validate(target.read_bytes())
    info = {
        "path": str(target),
        "bytes": len(pack),
        "sha256": hashlib.sha256(pack).hexdigest(),
        "texture": {"path": str(args.texture), "width": width, "height": height,
                    "sha256": hashlib.sha256(args.texture.read_bytes()).hexdigest()},
        "audio": {"path": str(args.audio), "bytes": len(args.audio.read_bytes()),
                  "offset": audio_offset,
                  "sha256": hashlib.sha256(args.audio.read_bytes()).hexdigest()},
        "format": "TTA1: header 16 B, RGB565, 1-bit mask, Ogg Opus",
    }
    report = target.parent / "pack.json"
    report.write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(info, ensure_ascii=False, indent=2))
    print(report)


if __name__ == "__main__":
    main()
