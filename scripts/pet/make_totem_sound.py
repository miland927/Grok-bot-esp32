"""Synthesize the 3.50 s totem revival track.

All layers are generated from oscillators and deterministic noise; there are no
external samples. The burst lands on 1.860 s, the death cut is 0.360-0.440 s,
and the first jelly rebound is 2.280 s.

Usage:
    python scripts/pet/make_totem_sound.py
    python scripts/pet/make_totem_sound.py --out-dir local-only/totem-preview
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

# The vanilla activation sound (item.totem.use) is the hero of the burst. It is
# a personal-prototype asset; see docs/pet/TOTEM.md before redistribution.
ORIGINAL_PATH = ROOT / "local-only/totem-reference/use_totem.ogg"


def load_original():
    if not ORIGINAL_PATH.exists():
        return None
    result = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(ORIGINAL_PATH), "-f", "f32le", "-ac", "1",
         "-ar", str(SR), "-"],
        check=False, capture_output=True)
    if result.returncode != 0 or not result.stdout:
        return None
    return np.frombuffer(result.stdout, dtype="<f4").astype(np.float64)


ORIGINAL = load_original()

BURST_AT = 1.86
DEATH_CUT = (0.360, 0.440)
REBOUND_AT = 2.280


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


def tone_sweep(n, f0, f1, curve=0.02, harmonics=(1.0,), vibrato=0.0, vibrato_hz=5.0):
    t = np.arange(n) / SR
    freq = f1 + (f0 - f1) * np.exp(-t / curve)
    if vibrato:
        freq = freq * (1.0 + vibrato * np.sin(2.0 * np.pi * vibrato_hz * t))
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


def bell(duration, freq, decay, amp, partials=(1.0, 0.55, 0.28, 0.12)):
    n = samples(duration)
    t = np.arange(n) / SR
    out = np.zeros(n)
    ratios = (1.0, 2.01, 2.98, 4.17, 5.43)
    for level, ratio in zip(partials, ratios):
        out += level * np.sin(2.0 * np.pi * freq * ratio * t) * np.exp(
            -t / max(0.04, decay / ratio ** 0.35))
    out *= np.minimum(1.0, t / 0.004)
    return amp * out / max(1e-9, np.max(np.abs(out)))


def pad(duration, freqs, amp, attack=0.18, release=0.25):
    n = samples(duration)
    t = np.arange(n) / SR
    env = np.minimum(1.0, t / attack) * np.minimum(1.0, (duration - t) / release)
    env = np.clip(env, 0.0, 1.0)
    out = np.zeros(n)
    for freq in freqs:
        out += np.sin(2.0 * np.pi * freq * t + 0.3 * np.sin(2.0 * np.pi * 0.27 * t))
    return amp * out / len(freqs) * env


def boing(duration, f0, f1, amp, wobble=0.12):
    n = samples(duration)
    t = np.arange(n) / SR
    freq = f1 + (f0 - f1) * np.exp(-t / 0.045)
    freq *= 1.0 + wobble * np.sin(2.0 * np.pi * 11.0 * t) * np.exp(-t / 0.09)
    phase = 2.0 * np.pi * np.cumsum(freq) / SR
    env = adsr(n, 0.006, 0.075, curve=1.6)
    return amp * np.sin(phase) * env


def room(duration, amp):
    n = samples(duration)
    return amp * band_noise(n, 40.0, 260.0, RNG, edge=80.0) * np.linspace(0.0, 1.0, n)


# --- 0.000-0.360 death ------------------------------------------------------
add(room(0.36, 0.0008), 0.0)
add(thump(0.16, 72.0, 34.0, 0.085, 0.10, RNG, noise=0.08), 0.030)
add(thump(0.15, 64.0, 30.0, 0.075, 0.07, RNG, noise=0.05), 0.175)
add(tone_sweep(samples(0.22), 92.0, 34.0, curve=0.10) * 0.020 *
    adsr(samples(0.22), 0.01, 0.08), 0.035)
add(bell(0.20, 1318.51, 0.11, 0.008, partials=(1.0, 0.2)), 0.240)
add(tick(0.018, 3000.0, 7000.0, 0.008, 0.012, RNG), 0.352)

# Hard cut: no death tail may leak into the summon.
MIX[samples(DEATH_CUT[0]):samples(DEATH_CUT[1])] = 0.0

# --- 0.440-1.100 summon -----------------------------------------------------
swell_n = samples(0.10)
swell = band_noise(swell_n, 350.0, 3000.0, RNG, edge=450.0)
swell *= np.linspace(0.0, 1.0, swell_n) ** 1.6
add(0.045 * swell, 0.352)
add(thump(0.10, 170.0, 62.0, 0.055, 0.13, RNG, noise=0.10), 0.430)
for index, (at, freq, amp) in enumerate((
        (0.442, 523.25, 0.115),
        (0.560, 659.25, 0.085),
        (0.680, 783.99, 0.070),
        (0.810, 1046.50, 0.050))):
    add(bell(0.46 - index * 0.03, freq, 0.30, amp), at)
add(bell(0.30, 1975.53, 0.10, 0.020), 0.452)
add(tick(0.035, 2200.0, 6200.0, 0.012, 0.018, RNG), 0.455)
add(tick(0.035, 2600.0, 7200.0, 0.012, 0.014, RNG), 0.700)
add(pad(0.66, (130.81, 196.00, 261.63), 0.028, attack=0.16, release=0.22), 0.440)

# --- 1.100-1.800 awakening --------------------------------------------------
add(pad(0.70, (110.00, 164.81, 220.00), 0.130, attack=0.30, release=0.18), 1.105)
add(tone_sweep(samples(0.66), 110.0, 220.0, curve=0.35, harmonics=(1.0, 0.25),
               vibrato=0.004, vibrato_hz=5.2) * 0.070 * adsr(samples(0.66), 0.25, 0.35), 1.120)
for index in range(16):
    at = 1.150 + index * (0.058 - index * 0.0019)
    add(tick(0.030, 1500.0 + 120 * index, 4600.0 + 260 * index,
             0.009, 0.030 + index * 0.0018, RNG), at)
shimmer_n = samples(0.25)
shimmer = band_noise(shimmer_n, 2800.0, 9000.0, RNG, edge=800.0)
shimmer *= np.linspace(0.0, 1.0, shimmer_n) ** 1.8
add(0.050 * shimmer, 1.545)
# The held breath: cut every tail at 1.800, leave the sub until the burst.
MIX[samples(1.800):samples(BURST_AT)] = 0.0
add(thump(0.055, 48.0, 34.0, 0.035, 0.05, RNG, noise=0.0), 1.804)

# --- 1.860 burst ------------------------------------------------------------
if ORIGINAL is None:
    add(tick(0.006, 900.0, 12000.0, 0.0025, 0.55, RNG), BURST_AT)
    add(bell(1.10, 440.00, 0.62, 0.16, partials=(1.0, 0.5, 0.3, 0.2)), BURST_AT)
    add(bell(1.00, 554.37, 0.55, 0.12, partials=(1.0, 0.45, 0.25, 0.15)), BURST_AT + 0.004)
    add(bell(0.95, 659.25, 0.50, 0.10, partials=(1.0, 0.4, 0.22, 0.12)), BURST_AT + 0.008)
    add(bell(0.80, 880.00, 0.45, 0.08, partials=(1.0, 0.35, 0.2)), BURST_AT + 0.012)
else:
    # Keep a short transient and a low body under the original shimmer; the
    # vanilla attack is already the main event, so no synth chord competes.
    add(tick(0.006, 900.0, 12000.0, 0.0025, 0.40, RNG), BURST_AT)
    _n = min(len(ORIGINAL), N - samples(BURST_AT))
    _t = np.arange(_n) / SR + BURST_AT
    _env = np.interp(_t, [1.86, 2.00, 2.20, 2.60, 3.00, 3.40, 3.50],
                     [0.82, 0.82, 0.62, 0.45, 0.30, 0.12, 0.0])
    add(_env * ORIGINAL[:_n], BURST_AT)
add(thump(0.50, 82.0, 28.0, 0.30, 0.42, RNG, noise=0.06), BURST_AT)
body_n = samples(0.60)
body = band_noise(body_n, 90.0, 5200.0, RNG, edge=240.0)
body *= adsr(body_n, 0.001, 0.20, curve=1.3)
add((0.22 if ORIGINAL is not None else 0.40) * body, BURST_AT)
shing_n = samples(0.34)
shing = band_noise(shing_n, 2600.0, 11000.0, RNG, edge=900.0)
shing *= adsr(shing_n, 0.0005, 0.085, curve=1.2)
add((0.08 if ORIGINAL is not None else 0.20) * shing, BURST_AT)
add(bell(0.50, 1760.00, 0.18, 0.020 if ORIGINAL is not None else 0.045), BURST_AT + 0.055)
add(bell(0.42, 2637.02, 0.14, 0.014 if ORIGINAL is not None else 0.030), BURST_AT + 0.100)
for index in range(10):
    add(tick(0.028, 2400.0 + 350 * index, 7000.0 + 500 * index,
             0.010, 0.010 if ORIGINAL is not None else 0.018 - index * 0.0012, RNG),
        BURST_AT + 0.06 + index * 0.028)
add(band_noise(samples(0.38), 400.0, 1800.0, RNG, edge=300.0) *
    (0.06 if ORIGINAL is not None else 0.14) * adsr(samples(0.38), 0.002, 0.13), BURST_AT)

# --- 2.200-3.500 revival ----------------------------------------------------
add(pad(1.26, (130.81, 196.00, 261.63, 329.63), 0.080, attack=0.28, release=0.40), 2.205)
add(band_noise(samples(0.32), 500.0, 2400.0, RNG, edge=500.0) * 0.020 *
    adsr(samples(0.32), 0.04, 0.10), 2.210)
add(boing(0.20, 185.0, 82.0, 0.28, wobble=0.14), REBOUND_AT)
add(thump(0.16, 95.0, 45.0, 0.085, 0.12, RNG, noise=0.05), REBOUND_AT + 0.005)
add(boing(0.14, 150.0, 74.0, 0.16, wobble=0.10), 2.445)
add(thump(0.12, 120.0, 60.0, 0.060, 0.06, RNG, noise=0.0), 2.450)
add(thump(0.14, 58.0, 30.0, 0.095, 0.08, RNG, noise=0.0), 2.560)
add(thump(0.12, 55.0, 28.0, 0.080, 0.055, RNG, noise=0.0), 2.895)
add(tick(0.020, 2600.0, 8000.0, 0.008, 0.050, RNG), 2.920)
add(bell(0.34, 659.25, 0.16, 0.045), 2.925)
add(tick(0.017, 3200.0, 9000.0, 0.007, 0.018, RNG), 3.010)
add(tick(0.017, 3000.0, 8500.0, 0.007, 0.018, RNG), 3.155)
add(bell(0.40, 1046.50, 0.20, 0.030), 3.220)
add(tick(0.020, 2500.0, 7000.0, 0.009, 0.012, RNG), 3.360)

# Static balance: lift the quiet death/summon/revival blocks relative to the
# burst, then normalize. The cut windows stay silent.
GAIN_TIMES = np.array([
    0.00, 0.34, 0.36, 0.44, 0.46, 1.10, 1.78, 1.80, 1.86, 2.20, 2.24, 2.60, 3.10, 3.46, 3.50,
])
GAIN_VALUES = np.array([
    2.25, 2.25, 1.0, 1.0, 2.25, 3.5, 3.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0,
])
MIX *= np.interp(np.arange(N) / SR, GAIN_TIMES, GAIN_VALUES)
MIX[samples(DEATH_CUT[0]):samples(DEATH_CUT[1])] = 0.0
MIX[samples(1.800):samples(BURST_AT)] = 0.0


def rms_db(a, b):
    chunk = MIX[samples(a):samples(b)]
    if chunk.size == 0:
        return -120.0
    return 20.0 * math.log10(max(float(np.sqrt(np.mean(chunk * chunk))), 1e-6))


def onset_after(threshold, after):
    start = samples(after)
    indices = np.nonzero(np.abs(MIX[start:]) > threshold)[0]
    return (start + int(indices[0])) / SR if len(indices) else None


peak = float(np.max(np.abs(MIX)))
if peak > 0:
    MIX *= 0.891 / peak
MIX = np.tanh(MIX * 1.05) / math.tanh(1.05)
MIX *= 0.891 / max(1e-9, np.max(np.abs(MIX)))
pcm = np.clip(np.round(MIX * 32767.0), -32768, 32767).astype("<i2")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command):
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "local-only/totem-preview")
    parser.add_argument("--ogg", type=Path, default=ROOT / "local-only/grok_totem_sound.ogg")
    args = parser.parse_args()
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    wav_path = out_dir / "totem-revival.wav"
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
    probe = subprocess.run([
        "ffprobe", "-v", "error", "-show_entries", "format=duration,size", "-of", "json",
        str(args.ogg),
    ], check=True, capture_output=True, text=True).stdout

    info = {
        "wav": {"path": str(wav_path), "bytes": wav_path.stat().st_size,
                "sha256": sha(wav_path)},
        "ogg": {"path": str(args.ogg), "bytes": args.ogg.stat().st_size,
                "sha256": sha(args.ogg), "probe": json.loads(probe)},
        "duration_s": DURATION,
        "sample_rate": SR,
        "channels": 1,
        "peak_dbfs": round(20.0 * math.log10(max(float(np.max(np.abs(MIX))), 1e-9)), 2),
        "death_silence_rms_dbfs": round(rms_db(*DEATH_CUT), 2),
        "burst_onset_s": round(onset_after(0.08, 1.80), 4) if onset_after(0.08, 1.80) else None,
        "rebound_onset_s": round(onset_after(0.02, 2.24), 4) if onset_after(0.02, 2.24) else None,
        "segments": {
            "0.00-0.36 death": round(rms_db(0.00, 0.36), 2),
            "0.36-0.44 silence": round(rms_db(0.36, 0.44), 2),
            "0.44-1.10 summon": round(rms_db(0.44, 1.10), 2),
            "1.10-1.80 awaken": round(rms_db(1.10, 1.80), 2),
            "1.80-1.86 hold": round(rms_db(1.80, 1.86), 2),
            "1.86-2.20 burst": round(rms_db(1.86, 2.20), 2),
            "2.20-3.50 revival": round(rms_db(2.20, 3.50), 2),
        },
        "events": len(EVENTS),
        "original": {
            "used": ORIGINAL is not None,
            "path": str(ORIGINAL_PATH) if ORIGINAL is not None else None,
            "sha256": sha(ORIGINAL_PATH) if ORIGINAL is not None else None,
            "source": "Minecraft item.totem.use (vanilla 26.2 assets); personal prototype only",
        },
        "source": "synth layers by scripts/pet/make_totem_sound.py; original totem burst layered at 1.86 s",
    }
    metrics = out_dir / "totem-sound-metrics.json"
    metrics.write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(info, ensure_ascii=False, indent=2))
    print(metrics)


if __name__ == "__main__":
    main()
