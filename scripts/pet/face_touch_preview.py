"""Before/after preview for the GrokBot ball's touch rebound.

Compares the old stiffness (squash k=105, d=15, 1.0 press impulse, no eye lag)
with the jelly springs (k=160, d=8.5, 2.5 press impulse, clamped eye lag).
The face silhouette is the same 240x240 procedural face; the video shows the
press, slow drag, held deformation and release at 33 ms and 50 ms.
"""

import argparse
import json
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W = H = 240
BG = (18, 18, 18)
FACE = (246, 238, 246)
RED = (220, 60, 60)
PANEL_W, PANEL_H = 560, 330
FACE_TOP = 42
LEFT_X, RIGHT_X = 15, 305
S = 3

AA_EXTENT = 1.025
EDGE_MARGIN = 2.0
SOFT_KNEE = 6.0
EYE_WIDTH = 0.135
EYE_HEIGHT = 0.165
EYE_SPACING = 0.34

OLD = dict(squash_k=105.0, squash_d=15.0, press=1.0, eye_lag=False)
NEW = dict(squash_k=160.0, squash_d=8.5, press=2.5, eye_lag=True)
EYE_K = 45.0
EYE_D = 9.5
EYE_SCALE = 0.40
EYE_CLAMP = 4.0


def clamp(value, low, high):
    return max(low, min(high, value))


def constrain_vertical_radius(center_y, radius_y):
    top_limit = (center_y - EDGE_MARGIN) / AA_EXTENT
    bottom_limit = (H - 1.0 - EDGE_MARGIN - center_y) / AA_EXTENT
    limit = min(top_limit, bottom_limit)
    if limit <= 0.0:
        return 0.5
    if radius_y <= limit:
        return radius_y
    over = radius_y - limit
    constrained = limit - SOFT_KNEE * (1.0 - math.exp(-over / SOFT_KNEE))
    return max(0.5, constrained)


class Motion:
    def __init__(self, config):
        self.config = config
        self.squash = 0.0
        self.squash_v = 0.0
        self.hop = 0.0
        self.hop_v = 0.0
        self.eye_hop = 0.0
        self.eye_hop_v = 0.0

    @staticmethod
    def spring(position, velocity, target, dt, stiffness, damping):
        acceleration = (target - position) * stiffness - velocity * damping
        velocity += acceleration * dt
        position += velocity * dt
        return position, velocity

    def press(self):
        self.squash_v -= self.config["press"]

    def step(self, pull, dt):
        self.squash, self.squash_v = self.spring(
            self.squash, self.squash_v, -0.30 * pull, dt,
            self.config["squash_k"], self.config["squash_d"],
        )
        self.hop, self.hop_v = self.spring(self.hop, self.hop_v, 16.0 * pull, dt, 60.0, 8.5)
        self.hop = clamp(self.hop, -36.0, 36.0)
        if self.config["eye_lag"]:
            self.eye_hop, self.eye_hop_v = self.spring(
                self.eye_hop, self.eye_hop_v, self.hop, dt, EYE_K, EYE_D
            )
            self.eye_hop = clamp(self.eye_hop, -36.0, 36.0)
        else:
            self.eye_hop = self.hop
            self.eye_hop_v = 0.0

    def shape(self):
        cy = 120.0 + self.hop
        sq = clamp(self.squash, -0.48, 0.48)
        rx = min(88.0 * (1.0 + sq), W / 2 - 4.0)
        ry = constrain_vertical_radius(cy, 88.0 * (1.0 - sq))
        lag = clamp((self.eye_hop - self.hop) * EYE_SCALE, -EYE_CLAMP, EYE_CLAMP)
        return cy, rx, ry, lag


def render_face(shape):
    cy, rx, ry, lag = shape
    image = Image.new("RGB", (W * S, H * S), BG)
    draw = ImageDraw.Draw(image)
    draw.ellipse(((120 - rx) * S, (cy - ry) * S, (120 + rx) * S, (cy + ry) * S), fill=FACE)
    eye_dx = EYE_SPACING * rx
    eye_rx = EYE_WIDTH * rx
    eye_ry = EYE_HEIGHT * ry
    base_y = cy - 0.04 * ry + lag
    for ex in (120 - eye_dx, 120 + eye_dx):
        draw.ellipse(
            ((ex - eye_rx) * S, (base_y - eye_ry) * S,
             (ex + eye_rx) * S, (base_y + eye_ry) * S),
            fill=BG,
        )
    image = image.resize((W, H), Image.LANCZOS)
    draw = ImageDraw.Draw(image)
    draw.line((0, H - 1, W - 1, H - 1), fill=RED)
    return image


def font():
    try:
        return ImageFont.truetype("arial.ttf", 13)
    except OSError:
        return ImageFont.load_default()


def measure_release(motion, pull_after, dt):
    """Return overshoot, sign changes and settle time for one release."""
    values = []
    for _ in range(round(1.4 / dt)):
        motion.step(pull_after, dt)
        values.append(motion.squash)
    peak = max(values)
    signs = 0
    last = 0
    for value in values:
        sign = 1 if value > 0.005 else (-1 if value < -0.005 else 0)
        if sign and last and sign != last:
            signs += 1
        if sign:
            last = sign
    settle = 0.0
    for index, value in enumerate(values):
        if abs(value) > 0.01:
            settle = (index + 1) * dt
    return {"overshoot": round(peak, 4), "oscillations": signs, "settle_ms": round(settle * 1000)}


def build(fps):
    dt = 1.0 / fps
    motions = {"before": Motion(OLD), "after": Motion(NEW)}
    frames = []
    metrics = {"before": {}, "after": {}}

    def push(kind, note=""):
        frames.append(
            {
                "kind": kind,
                "note": note,
                "shapes": {name: m.shape() for name, m in motions.items()},
                "squash": {name: m.squash for name, m in motions.items()},
            }
        )

    def step(pull):
        for motion in motions.values():
            motion.step(pull, dt)

    def rest(seconds, note=""):
        for _ in range(round(seconds * fps)):
            step(0.0)
            push("rest", note)

    # Press: one impulse, hold long enough for the jelly to show.
    for index in range(round(0.70 * fps)):
        if index == 0:
            for motion in motions.values():
                motion.press()
        step(0.0)
        push("press")

    # Drag to full and hold.
    steps = max(1, round(0.8 * fps))
    for index in range(steps):
        pull = (index + 1) / steps
        step(pull)
        push("drag")
    for _ in range(round(0.30 * fps)):
        step(1.0)
        push("hold")

    for name in motions:
        config = OLD if name == "before" else NEW
        probe = Motion(config)
        probe.squash = motions[name].squash
        probe.squash_v = motions[name].squash_v
        metrics[name]["drag_release"] = measure_release(probe, 0.0, dt)

    # Release slowly and show the rebound.
    for _ in range(round(1.0 * fps)):
        step(0.0)
        push("release")

    # A quick drag-and-release.
    steps = max(1, round(0.1 * fps))
    for index in range(steps):
        step((index + 1) / steps)
        push("drag")
    for _ in range(round(0.15 * fps)):
        step(1.0)
        push("hold")
    for name in motions:
        config = OLD if name == "before" else NEW
        probe = Motion(config)
        probe.squash = motions[name].squash
        probe.squash_v = motions[name].squash_v
        metrics[name]["fast_release"] = measure_release(probe, 0.0, dt)
    for _ in range(round(1.0 * fps)):
        step(0.0)
        push("release")

    # Press-only response metric: use the recorded squash minimum.
    press_min = {name: 0.0 for name in motions}
    for frame in frames:
        if frame["kind"] == "press":
            for name, value in frame["squash"].items():
                press_min[name] = min(press_min[name], value)
    for name in motions:
        metrics[name]["press_squash"] = round(press_min[name], 4)
        eye_lag = max(abs(shape[3]) for shape in
                      [frame["shapes"][name] for frame in frames])
        metrics[name]["eye_lag"] = round(eye_lag, 2)
        metrics[name]["max_bottom"] = round(
            max(shape[0] + AA_EXTENT * shape[2]
                for shape in [frame["shapes"][name] for frame in frames]), 2)
    return frames, metrics


def panel(frame, metrics):
    image = Image.new("RGB", (PANEL_W, PANEL_H), (10, 10, 10))
    draw = ImageDraw.Draw(image)
    text_font = font()
    draw.text((LEFT_X, 18), "BEFORE  (stiff)", fill=(230, 230, 230), font=text_font)
    draw.text((RIGHT_X, 18), "AFTER  (jelly)", fill=(230, 230, 230), font=text_font)
    if frame["note"]:
        draw.text((LEFT_X, 3), frame["note"], fill=(220, 170, 80), font=text_font)
    image.paste(render_face(frame["shapes"]["before"]), (LEFT_X, FACE_TOP))
    image.paste(render_face(frame["shapes"]["after"]), (RIGHT_X, FACE_TOP))
    for x in (LEFT_X - 1, RIGHT_X - 1):
        draw.rectangle((x, FACE_TOP - 1, x + W, FACE_TOP + H), outline=(60, 60, 60))
    draw.text(
        (LEFT_X, 288),
        f"drag overshoot before={metrics['before']['drag_release']['overshoot']:.3f} "
        f"after={metrics['after']['drag_release']['overshoot']:.3f}   "
        f"osc before={metrics['before']['drag_release']['oscillations']} "
        f"after={metrics['after']['drag_release']['oscillations']}",
        fill=(170, 170, 170), font=text_font,
    )
    draw.text(
        (LEFT_X, 306),
        f"press before={metrics['before']['press_squash']:.3f} "
        f"after={metrics['after']['press_squash']:.3f}   "
        f"eye lag={metrics['after']['eye_lag']:.1f}px   red = y=239",
        fill=(130, 130, 130), font=text_font,
    )
    return image


def encode(path, frames, fps, metrics):
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pixel_format", "rgb24",
        "-video_size", f"{PANEL_W}x{PANEL_H}", "-framerate", str(fps),
        "-i", "pipe:0", "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p",
        str(path),
    ]
    with subprocess.Popen(command, stdin=subprocess.PIPE) as encoder:
        for frame in frames:
            encoder.stdin.write(panel(frame, metrics).tobytes())
        encoder.stdin.close()
        if encoder.wait() != 0:
            raise SystemExit(f"FFmpeg failed: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path,
                        default=Path("local-only/animation-source/pet-face"))
    parser.add_argument("--fps", type=int, nargs="+", default=[30, 20])
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    report = {}
    for fps in args.fps:
        frames, metrics = build(fps)
        suffix = "33ms" if fps >= 25 else "50ms"
        path = args.out_dir / f"face-touch-rebound-{suffix}.mp4"
        encode(path, frames, fps, metrics)
        report[suffix] = {"fps": fps, "path": str(path), "metrics": metrics}
        print(f"{path}: {len(frames)} frames")
        for name in ("before", "after"):
            m = metrics[name]
            print(f"  {name}: drag overshoot={m['drag_release']['overshoot']:.3f} "
                  f"osc={m['drag_release']['oscillations']} "
                  f"settle={m['drag_release']['settle_ms']}ms press={m['press_squash']:.3f} "
                  f"eye_lag={m['eye_lag']:.2f}px bottom={m['max_bottom']:.2f}")
    out = args.out_dir / "face-touch-rebound-metrics.json"
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()
