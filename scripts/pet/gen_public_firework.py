"""Generate the public GrokBot firework from original procedural sparks and audio."""

from itertools import groupby
import math
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import wave
import zlib


def main():
    destination = Path(__file__).resolve().parents[2] / "assets/grokbot/grok_fireworks_fallback.gra"
    with tempfile.TemporaryDirectory() as work:
        source = Path(work) / "burst.wav"
        track = Path(work) / "burst.ogg"
        rate = 48000
        samples = bytearray()
        for i in range(rate * 7 // 2):
            t = i / rate
            launch = t - 0.37
            burst = t - 1.4
            value = 0.0
            if 0 <= launch < 0.8:
                value += 0.18 * math.sin(math.tau * (145 * launch + 31 * launch**2)) * math.exp(-7 * launch)
            if 0 <= burst < 1:
                value += 0.3 * math.sin(math.tau * 75 * burst) * math.exp(-10 * burst)
            samples.extend(struct.pack("<h", round(value * 32767)))
        with wave.open(str(source), "wb") as wav:
            wav.setparams((1, 2, rate, 0, "NONE", ""))
            wav.writeframes(samples)
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(source), "-c:a", "libopus",
                        "-b:a", "40k", str(track)], check=True)
        audio = track.read_bytes()

    rng = random.Random(42593)
    sparks = [(rng.uniform(0, math.tau), rng.uniform(8, 30), rng.randrange(3)) for _ in range(56)]
    colors = (0x9ffb, 0xfde9, 0x75ff)
    payload = bytearray()
    for frame in range(70):
        pixels = [0] * (160 * 160)
        age = (frame - 28) / 20
        if 0 <= age < 1.65:
            expand = 1 - math.exp(-3.4 * age)
            for angle, radius, color in sparks:
                x = round(80 + math.cos(angle) * radius * expand)
                y = round(80 + math.sin(angle) * radius * expand + 5 * age * age)
                if (frame + color) % 5 or age < 0.7:
                    size = 2 if age < 0.8 else 1
                    for dy in range(size):
                        for dx in range(size):
                            pixels[(y + dy) * 160 + x + dx] = colors[color]
        for color, run in groupby(pixels):
            payload.extend(struct.pack("<HH", sum(1 for _ in run), color))
    offset = 12 + len(payload)
    payload.extend(audio)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(struct.pack("<4sII", b"GRA1", offset, zlib.crc32(payload)) + payload)
    print(f"{destination}: {destination.stat().st_size} bytes")


if __name__ == "__main__":
    main()
