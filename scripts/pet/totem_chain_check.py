"""Verify the latest-TNT -> totem linkage sample.

The firmware cut is at 3.10 s, the totem opens on the 0.40 s black hold, and
the totem burst keeps its local 1.86 s cue (4.96 s in the linkage).

Usage:
    python scripts/pet/totem_chain_check.py
"""

import json
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
VIDEO = ROOT / "local-only" / "totem-preview" / "tnt-latest-totem-chain-60fps-410x502.mp4"
WIDTH, HEIGHT = 410, 502
CANVAS = 240
FPS = 60


def run(command):
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(result.stderr)
    return result


def probe(path):
    out = run(["ffprobe", "-v", "error", "-show_entries",
               "stream=codec_type,width,height,r_frame_rate,nb_frames,duration",
               "-of", "json", str(path)]).stdout
    return json.loads(out)["streams"]


def frame_at(seconds):
    result = subprocess.run([
        "ffmpeg", "-v", "error", "-ss", str(seconds), "-i", str(VIDEO),
        "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-",
    ], check=False, capture_output=True)
    if result.returncode != 0:
        raise SystemExit(result.stderr.decode("utf-8", "replace"))
    data = result.stdout
    if len(data) != WIDTH * HEIGHT * 3:
        raise SystemExit(f"short frame at {seconds}: {len(data)} bytes")
    return np.frombuffer(data, dtype=np.uint8).reshape(HEIGHT, WIDTH, 3)


def luma(frame):
    return float(frame.astype(np.float32).mean() / 255.0)


def center_luma(frame):
    crop = frame[HEIGHT // 2 - 60:HEIGHT // 2 + 60, WIDTH // 2 - 60:WIDTH // 2 + 60]
    return float(crop.astype(np.float32).mean() / 255.0)


def main():
    if not VIDEO.exists():
        raise SystemExit(f"missing {VIDEO}")
    streams = probe(VIDEO)
    decode = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(VIDEO),
                             "-f", "null", "-"], capture_output=True, text=True)
    if decode.returncode != 0:
        raise SystemExit(decode.stderr)

    samples = {}
    for t in (2.40, 3.05, 3.15, 3.35, 3.55, 4.60, 4.96):
        frame = frame_at(t)
        samples[f"{t:.2f}"] = {"mean": round(luma(frame), 4),
                               "center": round(center_luma(frame), 4)}

    checks = {
        "duration_6_6": all(abs(float(s.get("duration", 0)) - 6.6) < 0.05 for s in streams),
        "frame_396": any(s.get("nb_frames") == "396" for s in streams),
        "tnt_blast_visible": samples["2.40"]["center"] > 0.20,
        "black_after_cut": samples["3.15"]["mean"] < 0.01 and samples["3.35"]["mean"] < 0.01,
        "totem_at_3_55": samples["3.55"]["mean"] > 0.005,
        "burst_flash_at_4_96": samples["4.96"]["center"] > 0.60,
    }
    report = {"video": str(VIDEO), "streams": streams, "samples": samples,
              "checks": checks, "passed": all(checks.values())}
    path = VIDEO.parent / "tnt-latest-totem-chain-check.json"
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(path)
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
