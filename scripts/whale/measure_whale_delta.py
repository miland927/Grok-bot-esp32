"""Offline codec size experiment. Does not create a device-playable format."""
import argparse
import json
import pathlib
import zlib

from PIL import Image, ImageChops


def measure(path):
    with Image.open(path) as animation:
        palette = animation.copy()
        previous = Image.new('L', animation.size)
        independent = delta_bytes = changed = raw_bytes = 0
        stream = bytearray()
        for i in range(animation.n_frames):
            animation.seek(i)
            rgba = animation.convert('RGBA')
            indexed = rgba.convert('RGB').quantize(palette=palette, dither=Image.Dither.NONE)
            # Palette index zero is explicitly reserved for transparency.
            # Shift opaque indices by one with a fixed 63-colour palette source.
            values = bytearray(indexed.tobytes())
            alpha = rgba.getchannel('A').tobytes()
            values = bytes(0 if a < 128 else min(v + 1, 255) for v, a in zip(values, alpha))
            frame = Image.frombytes('L', rgba.size, values)
            delta = ImageChops.subtract_modulo(frame, previous)
            encoded = zlib.compress(delta.tobytes(), 9)
            assert ImageChops.add_modulo(Image.frombytes('L', rgba.size, zlib.decompress(encoded)), previous).tobytes() == values
            independent += len(zlib.compress(values, 9)) + 4
            delta_bytes += len(encoded) + 4
            raw_bytes += len(values)
            changed += sum(v != 0 for v in delta.tobytes())
            stream.extend(delta.tobytes())
            previous = frame
        return dict(file=path.name, frames=animation.n_frames, size=animation.size,
                    gif_bytes=path.stat().st_size, indexed_zlib_bytes=independent + 768,
                    delta_zlib_bytes=delta_bytes + 768,
                    delta_stream_bytes=len(zlib.compress(stream, 9)) + 768,
                    changed_fraction=round(changed/raw_bytes, 4),
                    note='Reversible relative to this quantized input; no firmware decoder or visual acceptance.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    rows = [measure(path) for path in args.inputs]
    args.output.write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(json.dumps(rows, indent=2))
