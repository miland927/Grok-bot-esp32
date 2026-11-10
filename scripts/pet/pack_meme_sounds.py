"""Pack a local, short Ogg Opus pool for the GrokBot PWR button."""

import argparse
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "local-only" / "grok_meme_bank.bin"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="manifest.json in the extracted sound library")
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    names = manifest["short_pool"]
    if not 2 <= len(names) <= 100 or len(set(names)) != len(names):
        parser.error("short_pool must contain 2–100 unique clips")

    clips = []
    offsets = [(len(names) + 2) * 4]
    for name in names:
        if not re.fullmatch(r"meme_[0-9]{3}\.ogg", name):
            parser.error(f"Invalid clip name: {name}")
        data = (args.manifest.parent / "clips" / name).read_bytes()
        if not data.startswith(b"OggS") or not 0 < len(data) <= 16 * 1024:
            parser.error(f"Invalid or oversized Ogg file: {name}")
        opus = data.find(b"OpusHead", 27, 128)
        if opus < 0 or data[opus + 9:opus + 10] != b"\x01":
            parser.error(f"Expected mono Opus: {name}")
        # The final Ogg granule counts decoded samples at 48 kHz.
        end = data.rfind(b"OggS")
        if end + 27 > len(data) or not data[end + 5] & 0x04:
            parser.error(f"Missing end-of-stream page: {name}")
        granule, = struct.unpack_from("<Q", data, end + 6)
        preskip, = struct.unpack_from("<H", data, opus + 10)
        if not 0 < granule - preskip <= 72_000:
            parser.error(f"Clip must be no longer than 1.5 seconds: {name}")
        clips.append(data)
        offsets.append(offsets[-1] + len(data))

    OUTPUT.parent.mkdir(exist_ok=True)
    temporary = OUTPUT.with_suffix(".tmp")
    temporary.write_bytes(struct.pack(f"<{len(offsets) + 1}I", len(names), *offsets) + b"".join(clips))
    temporary.replace(OUTPUT)
    print(f"{len(clips)} clips, {offsets[-1] / 1024:.1f} KiB: {OUTPUT}")


if __name__ == "__main__":
    main()
