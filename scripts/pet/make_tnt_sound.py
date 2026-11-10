"""Build the 3.50 s TNT track from Minecraft's own Fuse.ogg and Explosion1-4.ogg.

The fuse bed and explosion body are the original Minecraft samples. Only
level automation, a short reversed-fuse inhale, and very low supplemental
impact thumps are added; no synthesized layer carries the main identity.

Source files and hashes: local-only/minecraft-sfx/SOURCES.json
License: Minecraft assets are copyright Mojang/Microsoft. Personal,
non-distributed experiment only.

Usage:
    python scripts/pet/make_tnt_sound.py
    python scripts/pet/make_tnt_sound.py --primary 2
"""

import argparse
import hashlib
import json
import math
import subprocess
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SR = 48000
DURATION = 3.5
N = int(SR * DURATION)
SFX = ROOT / "local-only/minecraft-sfx"
PREVIEW = ROOT / "local-only/tnt-preview"
OGG = ROOT / "local-only/grok_tnt_blast.ogg"
EXPLOSIONS = (1, 2, 3, 4)


def db(value):
    return 20.0 * math.log10(max(float(value), 1e-9))


def rms(x):
    return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0


def decode(path):
    raw = subprocess.run([
        "ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-ac", "1",
        "-ar", str(SR), "-",
    ], check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0


def trim_lead(x, threshold_db=-50.0, keep=0.002):
    threshold = 10.0 ** (threshold_db / 20.0)
    hits = np.nonzero(np.abs(x) > threshold)[0]
    if not len(hits):
        return x
    start = max(0, int(hits[0] - keep * SR))
    return x[start:]


def fade(x, fade_in=0.0, fade_out=0.0):
    y = x.copy()
    if fade_in > 0:
        n = min(len(y), int(fade_in * SR))
        y[:n] *= np.linspace(0.0, 1.0, n)
    if fade_out > 0:
        n = min(len(y), int(fade_out * SR))
        y[-n:] *= np.linspace(1.0, 0.0, n)
    return y


def normalize_peak(x, peak=0.9):
    value = float(np.max(np.abs(x)))
    return x * (peak / value) if value > 1e-9 else x


def make_fuse():
    raw = trim_lead(decode(SFX / "Fuse.ogg"), threshold_db=-50.0)
    # 0.25-2.20 s needs 1.95 s; the original is 2.83 s.
    fuse = raw[:int(1.95 * SR)]
    fuse = fade(fuse, 0.012, 0.045)
    # The raw fuse is a quiet hiss with a strong ignition burst. Compress the
    # crest factor and lift the later stages so the original hiss reads at a
    # small speaker level without the supplemental impacts taking over.
    fuse = fuse / max(1e-9, float(np.max(np.abs(fuse))))
    fuse = np.tanh(fuse * 20.0) / math.tanh(20.0)
    fuse *= (10.0 ** (-24.0 / 20.0)) / max(1e-9, rms(fuse))
    t = 0.25 + np.arange(len(fuse)) / SR
    gain = np.interp(
        t,
        [0.25, 0.30, 0.90, 1.00, 1.45, 1.55, 2.10, 2.20],
        [0.0, 0.75, 0.75, 1.45, 1.45, 1.80, 1.80, 0.0],
    )
    return fuse * gain


def impact(duration, f0, f1, amp):
    n = int(duration * SR)
    t = np.arange(n) / SR
    freq = f1 + (f0 - f1) * np.exp(-t / 0.02)
    phase = 2.0 * np.pi * np.cumsum(freq) / SR
    env = (1.0 - np.exp(-t / 0.0018)) * np.exp(-t / 0.035)
    body = np.sin(phase) + 0.32 * np.sin(2.0 * phase) + 0.12 * np.sin(3.0 * phase)
    body = np.tanh(body * 1.5)
    body *= env
    return amp * body / max(1e-9, float(np.max(np.abs(body))))


def make_inhale(fuse):
    n = int(0.105 * SR)
    start = int(0.52 * SR)
    segment = fuse[start:start + n].copy()
    segment = segment[::-1]
    segment = fade(segment, 0.010, 0.020)
    return normalize_peak(segment, 0.045)


def make_explosion(index):
    raw = decode(SFX / f"Explosion{index}.ogg")
    return normalize_peak(trim_lead(raw, threshold_db=-60.0, keep=0.0), 0.90)


def make_track(explosion_index):
    mix = np.zeros(N, dtype=np.float64)
    fuse = make_fuse()
    start = int(0.25 * SR)
    end = min(N, start + len(fuse))
    mix[start:end] += fuse[:end - start]

    # Supplemental, clearly secondary impacts at the three compression peaks.
    impacts = [
        (0.52, 90.0, 52.0, 10.0 ** (-28.0 / 20.0)),
        (0.88, 150.0, 90.0, 10.0 ** (-32.0 / 20.0)),
        (1.02, 84.0, 44.0, 10.0 ** (-26.0 / 20.0)),
        (1.42, 180.0, 70.0, 10.0 ** (-30.0 / 20.0)),
        (1.74, 78.0, 38.0, 10.0 ** (-24.0 / 20.0)),
        (1.92, 120.0, 55.0, 10.0 ** (-28.0 / 20.0)),
    ]
    for at, f0, f1, amp in impacts:
        sound = impact(0.085, f0, f1, amp)
        offset = int(at * SR)
        mix[offset:offset + len(sound)] += sound

    inhale = make_inhale(fuse)
    offset = int(2.20 * SR)
    mix[offset:offset + len(inhale)] += inhale

    # 2.300-2.350 s is absolute silence; the original explosion starts at
    # 2.350 with its leading silence trimmed.
    mix[int(2.300 * SR):int(2.350 * SR)] = 0.0
    explosion = make_explosion(explosion_index)
    offset = int(2.35 * SR)
    end = min(N, offset + len(explosion))
    mix[offset:end] += explosion[:end - offset]

    # Fade the original explosion tail over the return so the 3.50 s cut has
    # no click and the ball comes back into silence.
    fade_start = int(3.10 * SR)
    mix[fade_start:] *= np.linspace(1.0, 0.0, N - fade_start)

    mix = normalize_peak(np.tanh(mix * 1.02) / math.tanh(1.02), 0.891)
    return mix


def write_wav(path, samples):
    pcm = np.clip(np.round(samples * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SR)
        handle.writeframes(pcm.tobytes())


def run(command):
    subprocess.run(command, check=True)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def first_above(samples, threshold, after):
    start = int(after * SR)
    hits = np.nonzero(np.abs(samples[start:]) > threshold)[0]
    return (start + int(hits[0])) / SR if len(hits) else None


def segment_db(samples, start, end):
    return round(db(rms(samples[int(start * SR):int(end * SR)])), 2)


def encode_ogg(wav_path, ogg_path):
    run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav_path),
        "-c:a", "libopus", "-b:a", "64k", "-ar", "48000", "-ac", "1",
        "-application", "lowdelay", "-frame_duration", "20", str(ogg_path),
    ])


def mux_video(source, wav_path, target):
    run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
        "-i", str(wav_path), "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy",
        "-c:a", "aac", "-b:a", "128k", "-shortest", "-movflags", "+faststart",
        str(target),
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--primary", type=int, choices=EXPLOSIONS, default=1)
    parser.add_argument("--skip-variants", action="store_true")
    args = parser.parse_args()
    PREVIEW.mkdir(parents=True, exist_ok=True)

    sources = json.loads((SFX / "SOURCES.json").read_text(encoding="utf-8"))
    all_variants = {index: make_track(index) for index in EXPLOSIONS}
    primary = all_variants[args.primary]

    wav_path = PREVIEW / "tnt-charge-blast.wav"
    write_wav(wav_path, primary)
    encode_ogg(wav_path, OGG)

    info = {
        "primary_explosion": args.primary,
        "duration_s": DURATION,
        "sample_rate": SR,
        "channels": 1,
        "source": "Minecraft Fuse.ogg + Explosion1-4.ogg (Minecraft Wiki), with level automation and low supplemental impacts",
        "license": sources["license"],
        "sources": sources["files"],
        "audio": {
            "wav": {"path": str(wav_path), "bytes": wav_path.stat().st_size, "sha256": sha(wav_path)},
            "ogg": {"path": str(OGG), "bytes": OGG.stat().st_size, "sha256": sha(OGG)},
            "peak_dbfs": round(db(float(np.max(np.abs(primary)))), 2),
            "explosion_onset_s": round(first_above(primary, 0.08, 2.30), 4),
            "pause_dbfs": segment_db(primary, 2.300, 2.350),
            "fuse_rms_dbfs": segment_db(primary, 0.30, 2.15),
            "explosion_rms_dbfs": segment_db(primary, 2.35, 2.90),
            "segments": {
                "0.00-0.25": segment_db(primary, 0.00, 0.25),
                "0.25-0.45": segment_db(primary, 0.25, 0.45),
                "0.45-0.95": segment_db(primary, 0.45, 0.95),
                "0.95-1.50": segment_db(primary, 0.95, 1.50),
                "1.50-2.20": segment_db(primary, 1.50, 2.20),
                "2.20-2.30": segment_db(primary, 2.20, 2.30),
                "2.30-2.35": segment_db(primary, 2.30, 2.35),
                "2.35-2.55": segment_db(primary, 2.35, 2.55),
                "2.55-3.10": segment_db(primary, 2.55, 3.10),
                "3.10-3.50": segment_db(primary, 3.10, 3.50),
            },
        },
        "variants": {},
        "videos": {},
    }

    for index, samples in all_variants.items():
        path = PREVIEW / f"tnt-explosion{index}.wav"
        write_wav(path, samples)
        info["variants"][str(index)] = {
            "path": str(path),
            "bytes": path.stat().st_size,
            "sha256": sha(path),
            "explosion_onset_s": round(first_above(samples, 0.08, 2.30), 4),
            "explosion_rms_dbfs": segment_db(samples, 2.35, 2.90),
        }

    for fps in (60, 20):
        silent = PREVIEW / f"tnt-cinematic-{fps}fps-410x502.mp4"
        target = PREVIEW / f"tnt-cinematic-{fps}fps-410x502-sound.mp4"
        mux_video(silent, wav_path, target)
        video = {"path": str(target), "bytes": target.stat().st_size, "sha256": sha(target)}
        info["videos"][f"{fps}fps"] = video
        if not args.skip_variants:
            for index in (2, 3, 4):
                variant = PREVIEW / f"tnt-cinematic-{fps}fps-410x502-sound-explosion{index}.mp4"
                mux_video(silent, PREVIEW / f"tnt-explosion{index}.wav", variant)
                info["variants"][str(index)][f"video_{fps}"] = {
                    "path": str(variant),
                    "bytes": variant.stat().st_size,
                    "sha256": sha(variant),
                }

    info_path = PREVIEW / "tnt-sound-metrics.json"
    info_path.write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(info, ensure_ascii=False, indent=2))
    print(info_path)


if __name__ == "__main__":
    main()
