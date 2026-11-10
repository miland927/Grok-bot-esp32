"""Render, encode and verify the 410x502 totem previews.

The firmware totem module draws a 240x240 canvas. These videos place that
canvas at the centre of the real 410x502 panel, exactly like the device.

Usage:
    python scripts/pet/totem_finish.py
"""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
WIDTH, HEIGHT = 410, 502
CANVAS = 240
FPS = {60: 210, 20: 70}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, check=True):
    return subprocess.run(command, check=check, capture_output=True, text=True)


def probe(path):
    out = run([
        "ffprobe", "-v", "error", "-show_entries",
        "stream=codec_type,width,height,r_frame_rate,nb_frames,duration", "-of", "json", str(path),
    ]).stdout
    return json.loads(out)["streams"]


def decode_ok(path):
    result = run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path), "-f", "null", "-"],
                 check=False)
    if result.returncode != 0:
        raise SystemExit(f"decode failed: {path}: {result.stderr}")


def render(exe, out_dir, fps, aftermath=False):
    name = f"frames{fps}" + ("-aftermath" if aftermath else "") + ".rgb"
    target = out_dir / name
    command = [str(exe), str(target), str(fps)]
    if aftermath:
        command.append("aftermath")
    run(command)
    return target


def encode(raw, out_dir, fps, aftermath=False):
    tag = f"{fps}fps" + ("-aftermath" if aftermath else "")
    silent = out_dir / f"totem-cinematic-{tag}-410x502-silent.mp4"
    audio = ROOT / "local-only" / "grok_totem_sound.ogg"
    final = out_dir / f"totem-cinematic-{tag}-410x502.mp4"
    run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", f"{CANVAS}x{CANVAS}",
        "-framerate", str(fps), "-i", str(raw),
        "-vf", f"pad={WIDTH}:{HEIGHT}:{(WIDTH - CANVAS) // 2}:{(HEIGHT - CANVAS) // 2}:black",
        "-c:v", "libx264", "-crf", "15", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
        str(silent),
    ])
    run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(silent),
        "-i", str(audio), "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy",
        "-c:a", "aac", "-b:a", "128k", "-shortest", "-movflags", "+faststart", str(final),
    ])
    return silent, final


def analyse(raw_path, fps):
    raw = np.fromfile(raw_path, dtype=np.uint8)
    count = len(raw) // (CANVAS * CANVAS * 3)
    frames = raw[: count * CANVAS * CANVAS * 3].reshape(count, CANVAS, CANVAS, 3)
    mean = frames.astype(np.float32).mean(axis=(1, 2, 3)) / 255.0
    center = frames[:, 90:150, 90:150, :].astype(np.float32).mean(axis=(1, 2, 3)) / 255.0
    peak = int(np.argmax(center))
    flash_start = int(1.70 * fps)
    onset = int(np.argmax(center[flash_start:] > 0.70)) + flash_start
    dark = float(np.min(mean[int(1.80 * fps):int(1.86 * fps) + 1]))
    death_start = float(mean[1])
    death_end = float(mean[int(0.35 * fps)])
    revival = float(mean[int(3.40 * fps)])
    afterglow = float(mean[int(2.20 * fps)])
    return {
        "frames": count,
        "mean_luma_first": round(death_start, 4),
        "mean_luma_death_end": round(death_end, 4),
        "mean_luma_hold_min": round(dark, 4),
        "flash_frame": onset,
        "flash_peak_time_s": round(peak / fps, 4),
        "flash_onset_s": round(onset / fps, 4),
        "mean_luma_flash": round(float(mean[onset]), 4),
        "center_flash": round(float(center[onset]), 4),
        "mean_luma_afterglow": round(afterglow, 4),
        "mean_luma_revival_end": round(revival, 4),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=ROOT / "local-only/totem-preview/totem-preview.exe")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "local-only/totem-preview")
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    report = {"source": "scripts/pet/totem_finish.py", "canvas": CANVAS,
              "frame": [WIDTH, HEIGHT], "fps": {}, "checks": {}}
    for fps in (60, 20):
        raw = render(args.exe, args.out_dir, fps)
        silent, final = encode(raw, args.out_dir, fps)
        streams = probe(final)
        decode_ok(final)
        stats = analyse(raw, fps)
        report["fps"][str(fps)] = {
            "raw": str(raw),
            "silent": {"path": str(silent), "bytes": silent.stat().st_size, "sha256": sha(silent)},
            "final": {"path": str(final), "bytes": final.stat().st_size, "sha256": sha(final),
                      "streams": streams},
            "stats": stats,
        }
        if fps == 60:
            report["checks"]["flash_at_186"] = abs(stats["flash_onset_s"] - 1.86) <= 1.0 / 60.0
            report["checks"]["hold_is_still"] = stats["mean_luma_hold_min"] < 0.12
            report["checks"]["death_dims"] = stats["mean_luma_death_end"] < 0.10
            report["checks"]["revival_restores_face"] = stats["mean_luma_revival_end"] > 0.10

    # Aftermath entry: black hold then summon, for the TNT easter egg.
    raw = render(args.exe, args.out_dir, 60, aftermath=True)
    silent, final = encode(raw, args.out_dir, 60, aftermath=True)
    decode_ok(final)
    report["aftermath"] = {
        "silent": {"path": str(silent), "bytes": silent.stat().st_size, "sha256": sha(silent)},
        "final": {"path": str(final), "bytes": final.stat().st_size, "sha256": sha(final),
                  "streams": probe(final)},
        "stats": analyse(raw, 60),
    }

    report["checks"]["aftermath_opens_black"] = report["aftermath"]["stats"]["mean_luma_first"] < 0.01
    report["passed"] = all(report["checks"].values())
    path = args.out_dir / "totem-video-check.json"
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report["checks"], ensure_ascii=False, indent=2))
    print(json.dumps({k: v["stats"] for k, v in report["fps"].items()}, ensure_ascii=False, indent=2))
    print(path)
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
