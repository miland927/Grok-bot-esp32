"""Pack GrokBot fireworks for microSD or generate the built-in fallback."""

import argparse
from itertools import groupby
import json
from pathlib import Path
import struct
import zlib

from PIL import Image, ImageSequence

EFFECTS = ("creeper", "star", "sphere", "burst", "rings", "willow", "heart", "snowflake")
TRACKS = ("blast", "sparkle", "large-sparkle")
FALLBACK = "creeper"

SIZE = (160, 160)
FRAME_COUNT = 70
FRAME_MS = 50
MAX_PACK_BYTES = 512 * 1024


def rgb565(pixel):
    r, g, b, alpha = pixel
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3) if alpha else 0


def pack_clip(gif, audio):
    opus = audio.find(b"OpusHead", 27, 128)
    if (not audio.startswith(b"OggS") or opus < 0 or audio[opus + 9:opus + 10] != b"\x01"
            or len(audio) > 32 * 1024):
        raise ValueError("Expected a mono Ogg Opus track of at most 32 KiB")

    payload = bytearray()
    count = 0
    with Image.open(gif) as image:
        if image.format != "GIF" or image.size != SIZE:
            raise ValueError(f"{gif}: expected a 160×160 GIF")
        for frame in ImageSequence.Iterator(image):
            duration = frame.info.get("duration", 0)
            repeat, remainder = divmod(duration, FRAME_MS)
            if not repeat or remainder or count + repeat > FRAME_COUNT:
                raise ValueError(f"{gif}: frame durations must fit the 50 ms timeline")
            rgba = frame.convert("RGBA")
            pixels = rgba.get_flattened_data() if hasattr(rgba, "get_flattened_data") else rgba.getdata()
            runs = bytearray()
            for color, run in groupby(map(rgb565, pixels)):
                runs.extend(struct.pack("<HH", sum(1 for _ in run), color))
            if 12 + len(payload) + len(runs) * repeat + len(audio) > MAX_PACK_BYTES:
                raise ValueError(f"{gif}: compressed pack exceeds 512 KiB")
            payload.extend(runs * repeat)
            count += repeat
    if count != FRAME_COUNT:
        raise ValueError(f"{gif}: expected a 3500 ms timeline")

    audio_offset = 12 + len(payload)
    payload.extend(audio)
    return struct.pack("<4sII", b"GRA1", audio_offset, zlib.crc32(payload)) + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("preview", type=Path, help="existing Grok fireworks preview directory")
    parser.add_argument("staging", type=Path, help="computer-side staging directory")
    parser.add_argument("--fallback-only", action="store_true",
                        help="generate the built-in fallback in the staging directory")
    args = parser.parse_args()

    timeline = json.loads((args.preview / "sound-timeline.json").read_text(encoding="utf-8"))
    expected_timing = {"duration_ms": 3500, "launch_ms": 370, "blast_ms": 1400, "twinkle_ms": 1850}
    if any(timeline.get(key) != value for key, value in expected_timing.items()):
        parser.error("Unexpected fireworks timeline")

    output = args.staging / "grokbot" / "fireworks"
    if not args.fallback_only and output.exists():
        parser.error(f"Use an empty staging directory. Already exists: {output}")
    packed = {}
    for effect in ((FALLBACK,) if args.fallback_only else EFFECTS):
        track = timeline["tracks"].get(effect)
        if track not in TRACKS:
            parser.error(f"Unknown track for {effect}: {track}")
        packed[effect] = pack_clip(args.preview / f"{effect}.gif",
                                   (args.preview / f"sound-{track}.ogg").read_bytes())
    if args.fallback_only:
        args.staging.mkdir(parents=True, exist_ok=True)
        path = args.staging / "grok_fireworks_fallback.gra"
        temporary = path.with_suffix(".tmp")
        temporary.write_bytes(packed[FALLBACK])
        temporary.replace(path)
        print(f"{path}: {len(packed[FALLBACK])} bytes")
        return

    output.mkdir(parents=True, exist_ok=False)
    for effect, data in packed.items():
        path = output / f"{effect}.gra"
        with path.open("xb") as file:
            file.write(data)
        print(f"{path.name}: {len(data)} bytes")
    print(output)


if __name__ == "__main__":
    main()
