"""Synthesize the 3.50 s TNT charge/blast track and mux the approved previews.

All layers are generated from deterministic noise and oscillators; there are no
external samples. The explosion starts exactly at 2.350 s, the 2.300-2.350 s
window is digital silence after the short inhale, and the charge tails are
closed before 2.200 s.

Usage (numpy required):
    python scripts/pet/make_tnt_sound.py
    python scripts/pet/make_tnt_sound.py --out-dir local-only/tnt-preview
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
RNG = np.random.default_rng(20261110)
MIX = np.zeros(N, dtype=np.float64)
EVENTS = []


def samples(seconds):
    return int(round(seconds * SR))


def add(signal, at):
    start = samples(at)
    end = min(N, start + len(signal))
    if start >= N or end <= start:
        return
    MIX[start:end] += signal[: end - start]
    EVENTS.append((round(at, 4), round(len(signal) / SR, 4)))


def adsr(n, attack, decay, curve=2.0):
    t = np.arange(n) / SR
    a = max(1, samples(attack))
    env = np.ones(n)
    env[:a] = np.linspace(0.0, 1.0, a, endpoint=False)
    env[a:] = np.exp(-((t[a:] - t[a]) / decay) ** curve)
    return env


def band_noise(n, low, high, rng, edge=180.0):
    x = rng.standard_normal(n)
    spectrum = np.fft.rfft(x)
    freq = np.fft.rfftfreq(n, 1.0 / SR)
    mask = np.ones_like(freq)
    mask[freq < low] = 0.0
    mask[freq > high] = 0.0
    if edge > 0:
        lo = (freq >= low) & (freq < low + edge)
        hi = (freq > high - edge) & (freq <= high)
        mask[lo] = 0.5 - 0.5 * np.cos(np.pi * (freq[lo] - low) / edge)
        mask[hi] = 0.5 + 0.5 * np.cos(np.pi * (freq[hi] - (high - edge)) / edge)
    y = np.fft.irfft(spectrum * mask, n)
    peak = np.max(np.abs(y))
    return y / peak if peak > 1e-9 else y


def tone_sweep(n, f0, f1, curve=0.02, harmonics=(1.0,)):
    t = np.arange(n) / SR
    freq = f1 + (f0 - f1) * np.exp(-t / curve)
    phase = 2.0 * np.pi * np.cumsum(freq) / SR
    out = np.zeros(n)
    for index, level in enumerate(harmonics, start=1):
        out += level * np.sin(phase * index)
    return out / max(1.0, sum(abs(x) for x in harmonics))


def thump(duration, f0, f1, decay, amp, rng, noise=0.0):
    n = samples(duration)
    body = tone_sweep(n, f0, f1, curve=0.025, harmonics=(1.0, 0.38, 0.16))
    env = adsr(n, 0.0015, decay)
    out = body * env
    if noise:
        puff = band_noise(n, 180.0, 1800.0, rng, edge=250.0) * adsr(n, 0.001, decay * 0.7)
        out = out * (1.0 - noise) + puff * noise
    out = np.tanh(out * 1.35)
    return amp * out / max(1e-9, np.max(np.abs(out)))


def tick(duration, low, high, decay, amp, rng):
    n = samples(duration)
    out = band_noise(n, low, high, rng, edge=min(400.0, (high - low) / 3))
    return amp * out * adsr(n, 0.0004, decay, curve=1.4)


def creak(duration, low, high, amp, rng, grains=5):
    n = samples(duration)
    out = np.zeros(n)
    for _ in range(grains):
        length = min(int(RNG.integers(samples(0.012), samples(0.04))), n)
        if length < 2:
            continue
        start = int(RNG.integers(0, n - length + 1))
        grain = band_noise(length, low, high, rng, edge=120.0)
        out[start:start + length] += grain * np.hanning(length)
    peak = np.max(np.abs(out))
    return amp * out / peak if peak > 1e-9 else out


def rumble(duration, low, high, amp, rng, attack=0.06, decay=1.1):
    n = samples(duration)
    out = band_noise(n, low, high, rng, edge=min(40.0, (high - low) / 2))
    env = adsr(n, attack, decay, curve=1.2)
    return amp * out * env


# --- 0.00-0.25 near silence: low room tone only ------------------------------
room = band_noise(samples(0.24), 40.0, 260.0, RNG, edge=80.0)
add(room * 0.0011 * adsr(len(room), 0.01, 0.3), 0.0)
add(tick(0.03, 1800, 5200, 0.012, 0.006, RNG), 0.185)

# --- 0.25-0.45 morph: stretch, creak, crate settles --------------------------
stretch_n = samples(0.17)
stretch = band_noise(stretch_n, 700.0, 3200.0, RNG, edge=500.0)
stretch *= np.linspace(0.15, 1.0, stretch_n) * adsr(stretch_n, 0.02, 0.07)
add(0.055 * stretch, 0.255)
add(tone_sweep(samples(0.12), 190.0, 80.0, curve=0.05) *
    0.035 * adsr(samples(0.12), 0.004, 0.055), 0.27)
add(creak(0.10, 500.0, 1800.0, 0.020, RNG, grains=4), 0.30)
add(thump(0.10, 120.0, 55.0, 0.045, 0.085, RNG, noise=0.12), 0.405)

# --- 0.45-0.95 charge 1: light squeeze and rebound ---------------------------
add(thump(0.12, 105.0, 52.0, 0.060, 0.095, RNG, noise=0.18), 0.505)
add(tick(0.035, 900.0, 3200.0, 0.012, 0.022, RNG), 0.560)
add(thump(0.11, 95.0, 48.0, 0.055, 0.082, RNG, noise=0.10), 0.755)
add(tick(0.04, 1100.0, 4200.0, 0.010, 0.018, RNG), 0.800)
add(thump(0.09, 160.0, 92.0, 0.035, 0.040, RNG, noise=0.0), 0.875)

# --- 0.95-1.50 charge 2: deeper, tighter, clearer rebound --------------------
add(thump(0.15, 82.0, 38.0, 0.085, 0.145, RNG, noise=0.16), 0.985)
add(creak(0.22, 260.0, 1200.0, 0.028, RNG, grains=6), 1.08)
add(thump(0.14, 74.0, 34.0, 0.075, 0.130, RNG, noise=0.12), 1.155)
add(tick(0.045, 700.0, 2600.0, 0.016, 0.026, RNG), 1.245)
add(thump(0.16, 90.0, 42.0, 0.090, 0.150, RNG, noise=0.14), 1.315)
add(thump(0.12, 190.0, 70.0, 0.055, 0.055, RNG, noise=0.10), 1.420)
add(tick(0.05, 1300.0, 4600.0, 0.018, 0.020, RNG), 1.465)

# --- 1.50-2.20 charge 3: dense ticks, pressure, fine tremble ----------------
add(rumble(0.68, 26.0, 115.0, 0.155, RNG, attack=0.18, decay=1.2), 1.50)
for index in range(11):
    at = 1.535 + index * 0.052
    level = 0.020 + index * 0.0022
    add(tick(0.035, 1200.0 + 90 * index, 4300.0 + 160 * index, 0.010, level, RNG), at)
add(creak(0.26, 300.0, 1600.0, 0.030, RNG, grains=7), 1.68)
add(thump(0.18, 70.0, 30.0, 0.100, 0.175, RNG, noise=0.15), 1.735)
add(creak(0.22, 380.0, 2200.0, 0.026, RNG, grains=6), 1.885)
add(thump(0.14, 110.0, 46.0, 0.070, 0.095, RNG, noise=0.10), 1.915)
shimmer_n = samples(0.25)
shimmer = band_noise(shimmer_n, 3500.0, 9000.0, RNG, edge=900.0)
shimmer *= np.linspace(0.1, 1.0, shimmer_n) * adsr(shimmer_n, 0.02, 0.10)
add(0.024 * shimmer, 1.93)
# Close every charge tail before the 2.20 s hold: the rumble ends at 2.18 s.
add(rumble(0.10, 40.0, 160.0, 0.030, RNG, attack=0.005, decay=0.05), 2.08)

# --- 2.20-2.30 inhale, then 2.30-2.35 absolute silence ----------------------
inhale_n = samples(0.095)
inhale = band_noise(inhale_n, 320.0, 2600.0, RNG, edge=600.0)
inhale *= np.linspace(0.12, 1.0, inhale_n) ** 1.4 * adsr(inhale_n, 0.018, 0.030, curve=1.2)
inhale_fade = samples(0.012)
inhale[-inhale_fade:] *= np.linspace(1.0, 0.0, inhale_fade)
add(0.050 * inhale, 2.20)

# Static balance: lift the three charge stages and the inhale relative to the
# blast, with smooth transitions so the quiet layers read on a small speaker.
GAIN_TIMES = np.array([
    0.00, 0.24, 0.26, 0.44, 0.46, 0.94, 0.96, 1.48, 1.52, 2.18, 2.20, 2.295,
    2.30, 2.60, 2.70, 3.05, 3.10, 3.48, 3.50,
])
GAIN_VALUES = np.array([
    1.0, 1.0, 1.8, 1.8, 1.7, 1.7, 1.5, 1.5, 2.0, 2.0, 5.0, 5.0,
    1.0, 1.0, 1.3, 1.3, 1.4, 1.4, 1.0,
])
MIX *= np.interp(np.arange(N) / SR, GAIN_TIMES, GAIN_VALUES)
MIX[samples(2.300):samples(2.350)] = 0.0

# --- 2.35 explosion ---------------------------------------------------------
blast_at = 2.35
n = samples(0.55)
crack = band_noise(samples(0.006), 700.0, 12000.0, RNG, edge=300.0)
crack *= np.exp(-np.arange(len(crack)) / (0.0018 * SR))
add(0.92 * crack, blast_at)
body = band_noise(n, 90.0, 5200.0, RNG, edge=220.0)
body *= adsr(n, 0.0008, 0.22, curve=1.35)
add(0.64 * body, blast_at)
sub_n = samples(0.55)
sub = tone_sweep(sub_n, 62.0, 28.0, curve=0.18, harmonics=(1.0, 0.30, 0.12))
sub *= adsr(sub_n, 0.001, 0.30, curve=1.1)
sub = np.tanh(sub * 1.4)
add(0.50 * sub / max(1e-9, np.max(np.abs(sub))), blast_at)
mid_n = samples(0.34)
mid = band_noise(mid_n, 130.0, 900.0, RNG, edge=150.0)
mid *= adsr(mid_n, 0.001, 0.10, curve=1.3)
add(0.38 * mid, blast_at)
add(tick(0.05, 2200.0, 9000.0, 0.012, 0.12, RNG), blast_at + 0.006)
sizzle_n = samples(0.22)
sizzle = band_noise(sizzle_n, 2500.0, 11000.0, RNG, edge=900.0)
sizzle *= adsr(sizzle_n, 0.0005, 0.055, curve=1.2)
add(0.18 * sizzle, blast_at)

# --- 2.55-3.10 aftermath ----------------------------------------------------
add(rumble(0.58, 28.0, 90.0, 0.115, RNG, attack=0.02, decay=1.3), 2.55)
for index, at in enumerate((2.58, 2.69, 2.81, 2.94, 3.06)):
    add(tick(0.055, 800.0 - 60 * index, 3600.0 - 300 * index, 0.020, 0.032 - index * 0.004, RNG), at)
air_n = samples(0.35)
air = band_noise(air_n, 1200.0, 5200.0, RNG, edge=500.0)
air *= np.linspace(0.5, 0.0, air_n) ** 1.3
add(0.022 * air, 2.76)

# --- 3.10-3.50 smoke/return -------------------------------------------------
add(rumble(0.40, 30.0, 70.0, 0.040, RNG, attack=0.01, decay=1.0), 3.10)
add(tick(0.06, 600.0, 2200.0, 0.025, 0.012, RNG), 3.255)
tail_n = samples(0.38)
tail = band_noise(tail_n, 60.0, 400.0, RNG, edge=50.0)
tail *= np.linspace(1.0, 0.0, tail_n) ** 1.7
add(0.007 * tail, 3.12)

# Normalize to -1 dBFS, with a gentle soft knee only if needed.
peak = float(np.max(np.abs(MIX)))
if peak > 0:
    MIX *= 0.891 / peak
MIX = np.tanh(MIX * 1.05) / math.tanh(1.05)
MIX *= 0.891 / max(1e-9, np.max(np.abs(MIX)))
pcm = np.clip(np.round(MIX * 32767.0), -32768, 32767).astype("<i2")


def rms_db(a, b):
    chunk = MIX[samples(a):samples(b)]
    if chunk.size == 0:
        return -120.0
    value = float(np.sqrt(np.mean(chunk * chunk)))
    return 20.0 * math.log10(max(value, 1e-6))


def first_above(threshold, after):
    start = samples(after)
    indices = np.nonzero(np.abs(MIX[start:]) > threshold)[0]
    return (start + int(indices[0])) / SR if len(indices) else None


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command):
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "local-only/tnt-preview")
    parser.add_argument("--ogg", type=Path, default=ROOT / "local-only/grok_tnt_blast.ogg")
    args = parser.parse_args()
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    wav_path = out_dir / "tnt-charge-blast.wav"
    with wave.open(str(wav_path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SR)
        handle.writeframes(pcm.tobytes())

    run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav_path),
        "-c:a", "libopus", "-b:a", "64k", "-ar", "48000", "-ac", "1",
        "-application", "lowdelay", "-frame_duration", "20", str(args.ogg),
    ])

    sound_info = {
        "wav": {"path": str(wav_path), "bytes": wav_path.stat().st_size,
                "sha256": sha(wav_path)},
        "ogg": {"path": str(args.ogg), "bytes": args.ogg.stat().st_size,
                "sha256": sha(args.ogg)},
        "duration_s": DURATION,
        "sample_rate": SR,
        "channels": 1,
        "peak_dbfs": round(20.0 * math.log10(max(float(np.max(np.abs(MIX))), 1e-9)), 2),
        "explosion_onset_s": round(first_above(0.08, 2.30), 4),
        "pause_rms_dbfs": round(rms_db(2.300, 2.350), 2),
        "segments": {
            "0.00-0.25": round(rms_db(0.00, 0.25), 2),
            "0.25-0.45": round(rms_db(0.25, 0.45), 2),
            "0.45-0.95": round(rms_db(0.45, 0.95), 2),
            "0.95-1.50": round(rms_db(0.95, 1.50), 2),
            "1.50-2.20": round(rms_db(1.50, 2.20), 2),
            "2.20-2.30": round(rms_db(2.20, 2.30), 2),
            "2.30-2.35": round(rms_db(2.30, 2.35), 2),
            "2.35-2.55": round(rms_db(2.35, 2.55), 2),
            "2.55-3.10": round(rms_db(2.55, 3.10), 2),
            "3.10-3.50": round(rms_db(3.10, 3.50), 2),
        },
        "events": len(EVENTS),
    }

    videos = {}
    for fps in (60, 20):
        source = out_dir / f"tnt-cinematic-{fps}fps-410x502.mp4"
        target = out_dir / f"tnt-cinematic-{fps}fps-410x502-sound.mp4"
        run([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
            "-i", str(wav_path), "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy",
            "-c:a", "aac", "-b:a", "128k", "-shortest", "-movflags", "+faststart",
            str(target),
        ])
        probe = subprocess.run([
            "ffprobe", "-v", "error", "-show_entries",
            "stream=codec_type,width,height,r_frame_rate,nb_frames,duration", "-of", "json",
            str(target),
        ], check=True, capture_output=True, text=True).stdout
        decode = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(target),
                                 "-f", "null", "-"], capture_output=True)
        if decode.returncode != 0:
            raise SystemExit(f"decode failed: {target}: {decode.stderr.decode()}")
        videos[f"{fps}fps"] = {"path": str(target), "bytes": target.stat().st_size,
                               "sha256": sha(target), "probe": json.loads(probe)}

    info = {"sound": sound_info, "videos": videos,
            "source": "synthesized by scripts/pet/make_tnt_sound.py; no external samples"}
    info_path = out_dir / "tnt-sound-metrics.json"
    info_path.write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(info, ensure_ascii=False, indent=2))
    print(info_path)


if __name__ == "__main__":
    main()
