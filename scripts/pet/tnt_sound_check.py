"""Check the original-Minecraft TNT track and the sound previews.

Verifies duration, the 2.350 s explosion onset, the 2.300-2.350 s silence,
the original Fuse/Explosion source hashes, the Ogg Opus size, and that the
white-flash frame in both muxed previews starts at the same moment as the
explosion.
"""

import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
PREVIEW = ROOT / "local-only/tnt-preview"
SR = 48000
DURATION = 3.5
EXPLOSION = 2.35
PAUSE = (2.300, 2.350)


def run(command, **kwargs):
    return subprocess.run(command, check=True, capture_output=True, **kwargs)


def decode_audio(path):
    raw = run([
        "ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-ac", "1", "-ar", str(SR),
        "-",
    ]).stdout
    return np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0


def decode_mp4_audio(path):
    raw = run([
        "ffmpeg", "-v", "error", "-i", str(path), "-vn", "-f", "s16le", "-ac", "1",
        "-ar", str(SR), "-",
    ]).stdout
    return np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0


def dbfs(value):
    return 20.0 * math.log10(max(value, 1e-9))


def rms(samples, start, end):
    chunk = samples[int(start * SR):int(end * SR)]
    return float(np.sqrt(np.mean(chunk * chunk))) if chunk.size else 0.0


def onset(samples, threshold=0.08, after=2.30):
    start = int(after * SR)
    hits = np.nonzero(np.abs(samples[start:]) > threshold)[0]
    return (start + int(hits[0])) / SR if len(hits) else None


def video_info(path):
    probe = json.loads(run([
        "ffprobe", "-v", "error", "-show_entries",
        "stream=codec_type,width,height,r_frame_rate,nb_frames,duration", "-of", "json",
        str(path),
    ]).stdout)
    return probe["streams"]


def flash_onset(path, fps):
    # The 240x240 face is centered in 410x502; the blast flash is white.
    raw = run([
        "ffmpeg", "-v", "error", "-i", str(path), "-vf", "crop=240:240:85:131",
        "-f", "rawvideo", "-pix_fmt", "gray", "-",
    ]).stdout
    frame_size = 240 * 240
    frames = len(raw) // frame_size
    values = []
    for index in range(frames):
        frame = np.frombuffer(raw[index * frame_size:(index + 1) * frame_size], dtype=np.uint8)
        values.append(float(frame.mean()))
    start = int(2.0 * fps)
    for index in range(start, frames):
        if values[index] >= 80.0:
            return index / fps, values
    return None, values


def main():
    failures = []
    info = json.loads((PREVIEW / "tnt-sound-metrics.json").read_text(encoding="utf-8"))
    wav = decode_audio(PREVIEW / "tnt-charge-blast.wav")
    ogg = decode_audio(ROOT / "local-only/grok_tnt_blast.ogg")

    def check(condition, label, detail=""):
        if not condition:
            failures.append(f"{label}: {detail}")

    sources = json.loads((ROOT / "local-only/minecraft-sfx/SOURCES.json").read_text(encoding="utf-8"))
    for name, meta in sources["files"].items():
        path = ROOT / "local-only/minecraft-sfx" / name
        check(path.exists(), f"source exists {name}", str(path))
        if path.exists():
            check(hashlib.sha256(path.read_bytes()).hexdigest() == meta["sha256"],
                  f"source hash {name}", meta["sha256"])

    for name, samples in (("wav", wav), ("ogg", ogg)):
        duration = len(samples) / SR
        check(abs(duration - DURATION) < 0.002, f"{name} duration", f"{duration:.4f}s")
        detected = onset(samples)
        check(detected is not None and abs(detected - EXPLOSION) <= 0.020,
              f"{name} explosion onset", f"{detected}")
        pause = rms(samples, *PAUSE)
        # The WAV is digitally silent; lossy Opus adds very low-level pre-echo
        # ahead of the blast, measured here well below audibility.
        limit = 1e-4 if name == "wav" else 10 ** (-45.0 / 20.0)
        check(pause < limit, f"{name} pause silence", f"{dbfs(pause):.1f} dBFS")
        peak = float(np.max(np.abs(samples)))
        check(peak <= 0.95, f"{name} clipping", f"{dbfs(peak):.2f} dBFS")
    fuse_rms = dbfs(rms(wav, 0.30, 2.15))
    explosion_rms = dbfs(rms(wav, 2.35, 2.90))
    check(-30.0 < fuse_rms < -15.0, "fuse bed audible", f"{fuse_rms:.1f} dBFS")
    check(explosion_rms > fuse_rms + 8.0, "explosion dominates the fuse",
          f"{explosion_rms:.1f}/{fuse_rms:.1f} dBFS")
    check(rms(wav, 0.0, 0.25) < 1e-4, "near-silent intro",
          f"{dbfs(rms(wav, 0.0, 0.25)):.1f} dBFS")
    ogg_size = (ROOT / "local-only/grok_tnt_blast.ogg").stat().st_size
    check(ogg_size <= 48 * 1024, "ogg size", f"{ogg_size} bytes")

    variants = {}
    for index in (1, 2, 3, 4):
        path = PREVIEW / f"tnt-explosion{index}.wav"
        samples = decode_audio(path)
        detected = onset(samples)
        check(detected is not None and abs(detected - EXPLOSION) <= 0.020,
              f"explosion{index} onset", f"{detected}")
        variants[str(index)] = {
            "path": str(path),
            "onset_s": round(detected, 4) if detected else None,
            "rms_dbfs": round(dbfs(rms(samples, 2.35, 2.90)), 2),
        }

    videos = {}
    for kind, pattern in (
        ("procedural", "tnt-cinematic-{fps}fps-410x502-sound.mp4"),
        ("texture", "tnt-cinematic-texture-{fps}fps-410x502-sound.mp4"),
    ):
        for fps in (60, 20):
            path = PREVIEW / pattern.format(fps=fps)
            streams = video_info(path)
            video = next((s for s in streams if s["codec_type"] == "video"), None)
            audio = next((s for s in streams if s["codec_type"] == "audio"), None)
            check(video is not None and video["width"] == 410 and video["height"] == 502,
                  f"{kind} {fps} video size", str(video))
            check(video is not None and abs(float(video["duration"]) - DURATION) < 0.01,
                  f"{kind} {fps} video duration", str(video))
            check(audio is not None and abs(float(audio["duration"]) - DURATION) < 0.01,
                  f"{kind} {fps} audio duration", str(audio))
            flash, values = flash_onset(path, fps)
            check(flash is not None and abs(flash - EXPLOSION) <= 1.0 / fps + 0.002,
                  f"{kind} {fps} flash onset", f"{flash}")
            mp4_audio = decode_mp4_audio(path)
            mp4_onset = onset(mp4_audio)
            check(mp4_onset is not None and abs(mp4_onset - EXPLOSION) <= 0.020,
                  f"{kind} {fps} mp4 audio onset", f"{mp4_onset}")
            decode = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path),
                                     "-f", "null", "-"], capture_output=True)
            check(decode.returncode == 0, f"{kind} {fps} decode", decode.stderr.decode()[-200:])
            videos[f"{kind}_{fps}fps"] = {
                "flash_onset_s": round(flash, 4) if flash else None,
                "audio_onset_s": round(mp4_onset, 4) if mp4_onset else None,
                "streams": streams,
            }

    report = {
        "passed": not failures,
        "failures": failures,
        "audio": {
            "wav_duration_s": round(len(wav) / SR, 4),
            "ogg_duration_s": round(len(ogg) / SR, 4),
            "explosion_onset_wav_s": round(onset(wav), 4),
            "explosion_onset_ogg_s": round(onset(ogg), 4),
            "pause_dbfs_wav": round(dbfs(rms(wav, *PAUSE)), 1),
            "pause_dbfs_ogg": round(dbfs(rms(ogg, *PAUSE)), 1),
            "peak_dbfs": round(dbfs(float(np.max(np.abs(wav)))), 2),
            "fuse_rms_dbfs": round(fuse_rms, 2),
            "explosion_rms_dbfs": round(explosion_rms, 2),
            "ogg_bytes": ogg_size,
            "ogg_sha256": info["audio"]["ogg"]["sha256"],
        },
        "sources": {name: meta["sha256"] for name, meta in sources["files"].items()},
        "variants": variants,
        "videos": videos,
    }
    out = PREVIEW / "tnt-sound-check.json"
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print("PASS: TNT sound and both previews align at 2.350 s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
