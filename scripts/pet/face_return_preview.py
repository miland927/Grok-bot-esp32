"""Before/after preview for the face drag clamp and the return bounce.

This mirrors the geometry and springs in main/display/pet_display.cc:
the 240x240 face canvas, the drag pull formula, squash/hop springs, the
return entrance, and the fixed -300 px/s upward impulse. It produces a
side-by-side MP4 and a metrics JSON with the maximum face-bottom Y and the
return overshoot at 33 ms and 50 ms sampling.
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
PANEL_W, PANEL_H = 540, 320
FACE_TOP = 42
LEFT_X, RIGHT_X = 15, 285
S = 3

AA_EXTENT = 1.025
EDGE_MARGIN = 2.0
SOFT_KNEE = 6.0
EYE_WIDTH = 0.135
EYE_HEIGHT = 0.165
EYE_SPACING = 0.34


def clamp(value, low, high):
    return max(low, min(high, value))


def smooth(t):
    t = clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


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
    def __init__(self, fixed):
        self.fixed = fixed
        self.hop = 0.0
        self.hop_v = 0.0
        self.squash = 0.0
        self.squash_v = 0.0

    @staticmethod
    def spring(position, velocity, target, dt, stiffness, damping):
        acceleration = (target - position) * stiffness - velocity * damping
        velocity += acceleration * dt
        position += velocity * dt
        return position, velocity

    def drag(self, pull, dt):
        self.squash, self.squash_v = self.spring(
            self.squash, self.squash_v, -0.30 * pull, dt, 105.0, 15.0
        )
        self.hop, self.hop_v = self.spring(self.hop, self.hop_v, 16.0 * pull, dt, 60.0, 8.5)
        self.hop = clamp(self.hop, -36.0, 36.0)

    def start_return(self):
        self.hop = 0.0
        self.hop_v = 0.0 if not self.fixed else -300.0
        self.squash = 0.0
        self.squash_v = 0.0

    def step_return(self, dt):
        self.hop, self.hop_v = self.spring(self.hop, self.hop_v, 0.0, dt, 60.0, 8.5)
        self.squash, self.squash_v = self.spring(
            self.squash, self.squash_v, 0.0, dt, 105.0, 15.0
        )

    def shape(self, entrance):
        settle = (1.0 - entrance) ** 3
        cy = 120.0 + self.hop + 65.0 * settle
        radius = 88.0 * (1.0 - 0.60 * settle)
        sq = clamp(self.squash, -0.48, 0.48)
        rx = min(radius * (1.0 + sq), W / 2 - 4.0)
        ry = radius * (1.0 - sq)
        if self.fixed and ry > 0:
            ry = constrain_vertical_radius(cy, ry)
        return cy, rx, ry


def render_face(shape, label=None, hidden=False):
    if hidden:
        image = Image.new("RGB", (W, H), BG)
        draw = ImageDraw.Draw(image)
        draw.text((16, H // 2 - 8), label or "fireworks 3.9 s (cut)", fill=(150, 150, 150))
    else:
        cy, rx, ry = shape
        image = Image.new("RGB", (W * S, H * S), BG)
        draw = ImageDraw.Draw(image)
        box = ((120 - rx) * S, (cy - ry) * S, (120 + rx) * S, (cy + ry) * S)
        draw.ellipse(box, fill=FACE)
        eye_dx = EYE_SPACING * rx
        eye_rx = EYE_WIDTH * rx
        eye_ry = EYE_HEIGHT * ry
        base_y = cy - 0.04 * ry
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


def build_timeline(fps, motions):
    """Return frames as (phase, pull, entrance, hidden, note) for both versions."""
    dt = 1.0 / fps
    frames = []
    states = {"before": motions["before"], "after": motions["after"]}
    entrance_time = {"before": 0.0, "after": 0.0}

    def push(phase, pull, entrance, hidden, note=""):
        frames.append(
            {
                "phase": phase,
                "pull": pull,
                "entrance": {"before": entrance["before"], "after": entrance["after"]},
                "hidden": hidden,
                "note": note,
                "shapes": {name: state.shape(entrance[name]) for name, state in states.items()},
            }
        )

    def rest(seconds, phase="rest", note=""):
        for _ in range(round(seconds * fps)):
            for state in states.values():
                state.drag(0.0, dt)
            push(phase, 0.0, {"before": 1.0, "after": 1.0}, False, note)

    def drag(seconds, start_pull, end_pull):
        steps = max(1, round(seconds * fps))
        for index in range(steps):
            pull = start_pull + (end_pull - start_pull) * (index + 1) / steps
            for state in states.values():
                state.drag(pull, dt)
            push("drag", pull, {"before": 1.0, "after": 1.0}, False)

    def hold(seconds, pull):
        for _ in range(round(seconds * fps)):
            for state in states.values():
                state.drag(pull, dt)
            push("hold", pull, {"before": 1.0, "after": 1.0}, False)

    def show(seconds):
        for _ in range(round(seconds * fps)):
            push("show", 1.0, {"before": 0.0, "after": 0.0}, True,
                 "fireworks 3.9 s (cut)")

    def start_return():
        for name, state in states.items():
            state.start_return()
            entrance_time[name] = 0.0

    def do_return(seconds):
        for index in range(round(seconds * fps)):
            for state in states.values():
                state.step_return(dt)
            entrance = {}
            for name in states:
                entrance_time[name] = min(1.0, entrance_time[name] + dt)
                entrance[name] = (
                    0.0
                    if index == 0
                    else min(1.0, entrance_time[name] / (0.24 if name == "after" else 0.40))
                )
            push("return", 0.0, entrance, False)

    def settle(seconds):
        for _ in range(round(seconds * fps)):
            for state in states.values():
                state.step_return(dt)
            push("settle", 0.0, {"before": 1.0, "after": 1.0}, False)

    # Slow pull, hold, launch, return.
    rest(0.30)
    drag(1.10, 0.0, 1.0)
    hold(0.50, 1.0)
    show(0.60)
    start_return()
    do_return(0.40)
    settle(0.65)
    # Fast pull, hold, launch, return.
    drag(0.10, 0.0, 1.0)
    hold(0.40, 1.0)
    show(0.50)
    start_return()
    do_return(0.40)
    settle(0.55)
    # Diagonal pull cancels (pull stays 0).
    rest(0.70, "cancel", "diagonal cancel: no launch")
    rest(0.20)
    return frames


def panel_frame(frame, title_before, title_after, metrics):
    image = Image.new("RGB", (PANEL_W, PANEL_H), (10, 10, 10))
    draw = ImageDraw.Draw(image)
    text_font = font()
    draw.text((LEFT_X, 18), title_before, fill=(230, 230, 230), font=text_font)
    draw.text((RIGHT_X, 18), title_after, fill=(230, 230, 230), font=text_font)
    if frame["note"]:
        draw.text((LEFT_X, 3), frame["note"], fill=(220, 170, 80), font=text_font)
    hidden = frame["hidden"]
    note = frame["note"]
    left = render_face(frame["shapes"]["before"], note, hidden)
    right = render_face(frame["shapes"]["after"], note, hidden)
    image.paste(left, (LEFT_X, FACE_TOP))
    image.paste(right, (RIGHT_X, FACE_TOP))
    for x in (LEFT_X - 1, RIGHT_X - 1):
        draw.rectangle((x, FACE_TOP - 1, x + W, FACE_TOP + H), outline=(60, 60, 60))
    metrics_text = (
        f"bottom before={metrics['before']['max_bottom_drag']:.1f}  "
        f"after={metrics['after']['max_bottom_drag']:.1f}   "
        f"return peak before={metrics['before']['bounce_peak']:.1f}  "
        f"after={metrics['after']['bounce_peak']:.1f}"
    )
    draw.text((LEFT_X, 288), metrics_text, fill=(170, 170, 170), font=text_font)
    draw.text((LEFT_X, 305), "red line = last face-canvas row (y=239)", fill=(130, 130, 130),
              font=text_font)
    return image


def measure(fps, motions, frames):
    dt = 1.0 / fps
    max_bottom = {"before": 0.0, "after": 0.0}
    min_center = {"before": 1e9, "after": 1e9}
    max_return_bottom = {"before": 0.0, "after": 0.0}
    for frame in frames:
        if frame["phase"] in ("drag", "hold") and not frame["hidden"]:
            for name, (cy, rx, ry) in frame["shapes"].items():
                max_bottom[name] = max(max_bottom[name], cy + AA_EXTENT * ry)
        if frame["phase"] in ("return", "settle"):
            for name, (cy, rx, ry) in frame["shapes"].items():
                min_center[name] = min(min_center[name], cy)
                max_return_bottom[name] = max(max_return_bottom[name], cy + AA_EXTENT * ry)
    return {
        name: {
            "max_bottom_drag": round(max_bottom[name], 2),
            "bounce_peak": round(min_center[name], 2),
            "overshoot": round(120.0 - min_center[name], 2),
            "max_bottom_return": round(max_return_bottom[name], 2),
        }
        for name in ("before", "after")
    }


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
            image = panel_frame(frame, "BEFORE  (no clamp)", "AFTER  (clamped)", metrics)
            encoder.stdin.write(image.tobytes())
        encoder.stdin.close()
        if encoder.wait() != 0:
            raise SystemExit(f"FFmpeg failed: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path,
                        default=Path("local-only/animation-source/pet-face"))
    parser.add_argument("--fps", type=int, nargs="+", default=[30, 20])
    args = parser.parse_args()
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    report = {}
    for fps in args.fps:
        motions = {"before": Motion(False), "after": Motion(True)}
        frames = build_timeline(fps, motions)
        metrics = measure(fps, motions, frames)
        suffix = "33ms" if fps >= 25 else "50ms"
        path = out_dir / f"face-drag-return-{suffix}.mp4"
        encode(path, frames, fps, metrics)
        report[suffix] = {"fps": fps, "path": str(path), "metrics": metrics}
        print(f"{path}: {len(frames)} frames")
        for name in ("before", "after"):
            m = metrics[name]
            print(f"  {name}: max bottom={m['max_bottom_drag']:.2f} "
                  f"return peak={m['bounce_peak']:.2f} overshoot={m['overshoot']:.2f} "
                  f"return bottom={m['max_bottom_return']:.2f}")

    report_path = out_dir / "face-drag-return-metrics.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(report_path)


if __name__ == "__main__":
    main()
