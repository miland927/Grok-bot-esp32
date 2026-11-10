"""Stage a GTN1 TNT pack for the existing read-only GrokBot SD library."""

import argparse
from pathlib import Path
import struct
import zlib

from gen_tnt_texture import read_texture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("texture", type=Path, help="16x16 TNT face PNG")
    parser.add_argument("audio", type=Path, help="mixed 3.5 s mono Ogg Opus")
    parser.add_argument("staging", type=Path, help="computer-side staging directory")
    args = parser.parse_args()

    track = args.audio.read_bytes()
    opus = track.find(b"OpusHead", 27, 128)
    if not track.startswith(b"OggS") or opus < 0 or track[opus + 9:opus + 10] != b"\x01" or len(track) > 32768:
        parser.error("expected mono Ogg Opus of at most 32 KiB")
    payload = struct.pack("<256H", *read_texture(args.texture)) + track
    packed = struct.pack("<4sI", b"GTN1", zlib.crc32(payload)) + payload
    out = args.staging / "grokbot" / "tnt" / "show.gtn"
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("xb") as file:
        file.write(packed)
    print(f"{out}: {len(packed)} bytes")


if __name__ == "__main__":
    main()
