"""Point-by-point cross-check of the firmware fireworks camera vs the v2 preview.

Compiles scripts/pet/fireworks_camera_sampler.cc with the pure camera module,
samples the same absolute show times as make_tracking_preview.py, and reports
the largest deviation. Also checks repeated/out-of-order sampling, skipped
ticks, the locked lens, the burst hand-off, and the flight-to-burst dirty rect.

Example:
    python scripts/pet/fireworks_flight_check.py
    python scripts/pet/fireworks_flight_check.py --source-dir local-only/animation-source/fireworks-v4
"""

import argparse
import importlib.util
import json
import math
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DISPLAY = ROOT / "main" / "display"
SAMPLER = ROOT / "scripts" / "pet" / "fireworks_camera_sampler.cc"
ASSETS_HEADER = DISPLAY / "pet_fireworks_assets.h"

KEY_TIMES = [0, 100, 250, 370, 470, 603, 748, 827, 912, 960, 990, 1020, 1100, 1200, 1300, 1400]
TOLERANCE = {
    "origin": 0.08,
    "zoom": 0.002,
    "screen": 0.08,
    "scale": 0.001,
    "offset": 0.02,
    "height": 0.15,
}
EFFECTS = ("creeper", "star", "sphere", "burst", "rings", "willow", "heart", "snowflake")
FIELDS = (
    "ms",
    "origin_x",
    "origin_y",
    "zoom",
    "rocket_x",
    "rocket_y",
    "screen_x",
    "screen_y",
    "scale",
    "offset",
    "progress",
    "height",
    "width",
    "anchor_x",
    "anchor_y",
    "start_zoom",
)


def import_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compiler_command(value):
    command = shlex.split(value)
    if not command or shutil.which(command[0]) is None:
        return None
    return command


def build_sampler(command, workdir):
    executable = workdir / "fireworks_camera_sampler.exe"
    subprocess.run(
        command
        + [
            "-std=c++20",
            f"-I{DISPLAY}",
            str(SAMPLER),
            str(DISPLAY / "pet_fireworks_camera.cc"),
            "-o",
            str(executable),
        ],
        check=True,
        cwd=workdir,
    )
    return executable


def run_sampler(executable, times, workdir):
    output = subprocess.run(
        [str(executable), *[f"{value:.6f}" for value in times]],
        check=True,
        capture_output=True,
        text=True,
        cwd=workdir,
    ).stdout
    rows = []
    for line in output.splitlines():
        values = [float(field) for field in line.split()]
        if len(values) != len(FIELDS):
            raise ValueError(f"Unexpected sampler row: {line!r}")
        rows.append(dict(zip(FIELDS, values)))
    return rows


def preview_expected(preview, ms):
    view = preview.lens(ms)
    rocket_x, rocket_y = preview.rocket_position(ms)
    screen_x, screen_y = preview.project(rocket_x, rocket_y, view)
    scale = preview.scale_at(ms)
    height = 37.0 * scale * view[2]
    return {
        "ms": ms,
        "origin_x": view[0],
        "origin_y": view[1],
        "zoom": view[2],
        "rocket_x": rocket_x,
        "rocket_y": rocket_y,
        "screen_x": screen_x,
        "screen_y": screen_y,
        "scale": scale,
        "offset": preview.camera_offset(ms),
        "height": height,
        "width": height * 8.0 / 13.0,
    }


def compare_rows(preview, rows):
    table = []
    worst = {key: 0.0 for key in TOLERANCE}
    for row in rows:
        expected = preview_expected(preview, row["ms"])
        delta = {
            "origin": max(abs(row["origin_x"] - expected["origin_x"]),
                          abs(row["origin_y"] - expected["origin_y"])),
            "zoom": abs(row["zoom"] - expected["zoom"]),
            "screen": max(abs(row["screen_x"] - expected["screen_x"]),
                          abs(row["screen_y"] - expected["screen_y"])),
            "scale": abs(row["scale"] - expected["scale"]),
            "offset": abs(row["offset"] - expected["offset"]),
            "height": abs(row["height"] - expected["height"]),
        }
        for key, value in delta.items():
            worst[key] = max(worst[key], value)
        table.append(
            {
                "ms": row["ms"],
                "origin": [round(row["origin_x"], 3), round(row["origin_y"], 3)],
                "zoom": round(row["zoom"], 5),
                "screen": [round(row["screen_x"], 3), round(row["screen_y"], 3)],
                "height": round(row["height"], 3),
                "offset": round(row["offset"], 3),
                "delta": {key: round(value, 5) for key, value in delta.items()},
            }
        )
    failures = [
        f"{key} deviation {worst[key]:.4f} > {limit}"
        for key, limit in TOLERANCE.items()
        if worst[key] > limit
    ]
    return table, worst, failures


def parse_assets_header():
    text = ASSETS_HEADER.read_text(encoding="utf-8")
    block = re.search(r"kBurstFitZoom\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not block:
        raise ValueError("kBurstFitZoom not found in generated assets header")
    return [float(value.rstrip("f")) for value in re.findall(r"[-+0-9.]+f", block.group(1))]


def flight_cluster(preview, ms):
    """Renderer-side cluster rect at a flight time, for the dirty-area check."""
    view = preview.lens(ms)
    rocket_x, rocket_y = preview.rocket_position(ms)
    screen_x, screen_y = preview.project(rocket_x, rocket_y, view)
    scale = preview.scale_at(ms)
    height = round(37 * scale * view[2])
    width = round(height * 8 / 13)
    left = round(screen_x - width * 0.31)
    top = round(screen_y - height * 0.52)
    rect = (left, top, left + width - 1, top + height - 1)
    strength = preview.smooth((ms - 250) / 205) * (1 - preview.smooth((ms - 1220) / (preview.BURST - 1220)))
    if strength <= 0:
        return rect
    zoom = view[2]
    nozzle_x, nozzle_y = screen_x - 3 * scale * zoom, screen_y + 10 * scale * zoom
    length = (9 + 3 * math.sin(ms / 23)) * scale * zoom * strength
    points = [(nozzle_x - 2.1 * scale * zoom, nozzle_y),
              (nozzle_x + 2 * scale * zoom, nozzle_y + 1),
              (nozzle_x - length * .36, nozzle_y + length)]
    rect = union(rect, (math.floor(min(p[0] for p in points)),
                        math.floor(min(p[1] for p in points)),
                        math.ceil(max(p[0] for p in points)),
                        math.ceil(max(p[1] for p in points))))
    progress = preview.travel(preview.sample(preview.ALTITUDE, preview.flight_time(ms)))
    for j in range(12, 0, -1):
        prior = ms - j * 19
        if prior < 250:
            continue
        trail_x, trail_y = preview.rocket_position(prior)
        trail_scale = preview.scale_at(prior)
        trail_x += math.sin(j * 1.6 + ms * .027) * j * .42 - 3 * trail_scale
        trail_y += 12 * trail_scale + j * (1 + progress)
        px, py = preview.project(trail_x, trail_y, view)
        radius = max(.5, zoom * scale * (2.6 - .20 * j) * strength)
        rect = union(rect, (math.floor(px - radius), math.floor(py - radius),
                            math.ceil(px + radius), math.ceil(py + radius)))
    return rect


def union(a, b):
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def contains(outer, inner):
    return outer[0] <= inner[0] and outer[1] <= inner[1] and outer[2] >= inner[2] and outer[3] >= inner[3]


def inside(rect, width, height):
    rect = (max(0, rect[0]), max(0, rect[1]), min(width - 1, rect[2]), min(height - 1, rect[3]))
    return rect[0] <= rect[2] and rect[1] <= rect[3]


def burst_footprint(preview, source_dir):
    """First burst frame footprint in screen pixels at the frozen burst lens."""
    from PIL import Image, ImageSequence

    frames = []
    elapsed = 0
    with Image.open(source_dir / "creeper.gif") as gif:
        for frame in ImageSequence.Iterator(gif):
            elapsed += frame.info["duration"]
            if elapsed > preview.BURST:
                box = frame.convert("RGB").getbbox()
                if box:
                    frames.append(box)
    view = preview.lens(preview.BURST)
    origin_x, origin_y = view[0], view[1]
    zoom = view[2]
    left, top, right, bottom = frames[0]
    return (math.floor(origin_x + left * zoom), math.floor(origin_y + top * zoom),
            math.ceil(origin_x + right * zoom), math.ceil(origin_y + bottom * zoom))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path,
                        default=ROOT / "local-only/animation-source/fireworks-v4")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--report", type=Path,
                        default=ROOT / "local-only/reports/fireworks_flight_check.json")
    args = parser.parse_args()

    source_dir = args.source_dir.resolve()
    preview_path = source_dir / "make_tracking_preview.py"
    if not preview_path.is_file():
        print(f"Preview source not found: {preview_path}", file=sys.stderr)
        return 2
    preview = import_module(preview_path, "make_tracking_preview")

    command = compiler_command(args.cxx)
    if command is None:
        print(f"No host C++ compiler found ({args.cxx}); cannot sample the firmware module.",
              file=sys.stderr)
        return 2

    times = sorted(set([float(value) for value in KEY_TIMES]
                       + [float(value) for value in range(0, 1401, 33)]))
    with __import__("tempfile").TemporaryDirectory(ignore_cleanup_errors=True) as directory:
        workdir = Path(directory)
        executable = build_sampler(command, workdir)
        rows = run_sampler(executable, times, workdir)
        repeated = run_sampler(executable, times, workdir)
        shuffled = run_sampler(executable, list(reversed(times)), workdir)

    failures = []
    if rows != repeated:
        failures.append("repeated sampling changed values")
    if rows != list(reversed(shuffled)):
        failures.append("sampling depends on call order")

    table, worst, compare_failures = compare_rows(preview, rows)
    failures.extend(compare_failures)

    anchor = rows[-1]
    anchor_x, anchor_y = anchor["anchor_x"], anchor["anchor_y"]
    start_zoom = anchor["start_zoom"]
    if abs(anchor_x - preview.project(*preview.rocket_position(preview.BURST),
                                      preview.lens(preview.BURST))[0]) > 2:
        failures.append("burst anchor x deviates from the rocket endpoint")
    if abs(anchor_y - preview.project(*preview.rocket_position(preview.BURST),
                                      preview.lens(preview.BURST))[1]) > 2:
        failures.append("burst anchor y deviates from the rocket endpoint")

    # The lens must be frozen from the brake lock to the burst.
    locked = [row for row in rows if 1020 <= row["ms"] <= 1400]
    if len({(round(row["origin_x"], 6), round(row["origin_y"], 6), round(row["zoom"], 6))
            for row in locked}) != 1:
        failures.append("lens moved between 1020 and 1400 ms")

    # Burst hand-off: first material frame index, pinned anchor, local dirty rect.
    burst_index = preview.BURST // 50
    if burst_index != 28:
        failures.append(f"first burst frame index is {burst_index}, expected 28")
    footprint = burst_footprint(preview, source_dir)
    cluster = flight_cluster(preview, preview.BURST - 0.001)
    dirty = union(cluster, footprint)
    if not contains(dirty, footprint) or not contains(dirty, cluster):
        failures.append("burst dirty rect does not cover both old and new pixels")
    if not inside(dirty, 410, 502):
        failures.append("burst dirty rect is outside the canvas")
    dirty_area = (dirty[2] - dirty[0] + 1) * (dirty[3] - dirty[1] + 1)
    if dirty_area > 410 * 502 // 2:
        failures.append("burst dirty rect covers more than half the canvas")

    # The generated per-effect burst fit zoom must match the sources.
    generator = import_module(ROOT / "scripts/pet/gen_firework_camera_assets.py",
                              "gen_firework_camera_assets")
    fits = parse_assets_header()
    for effect, header_zoom in zip(EFFECTS, fits):
        source_zoom = generator.effect_fit_zoom(source_dir, effect, preview)
        if abs(header_zoom - source_zoom) > 1e-4:
            failures.append(f"{effect} burst fit zoom {header_zoom} != {source_zoom}")

    # 33 ms rhythm: no discontinuity larger than the fastest near-view move.
    screen = [(row["screen_x"], row["screen_y"]) for row in rows]
    steps = [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(screen, screen[1:])]
    max_step = max(steps) if steps else 0.0

    print(f"{'ms':>7} {'origin':>20} {'zoom':>9} {'screen':>20} {'height':>8} {'offset':>7} "
          f"{'max d':>7}")
    for entry in table:
        delta = max(entry["delta"].values())
        print(f"{entry['ms']:7.0f} {entry['origin'][0]:9.2f},{entry['origin'][1]:9.2f} "
              f"{entry['zoom']:9.5f} {entry['screen'][0]:9.2f},{entry['screen'][1]:9.2f} "
              f"{entry['height']:8.2f} {entry['offset']:7.2f} {delta:7.4f}")
    print()
    print("worst deviations:", ", ".join(f"{key}={value:.4f}" for key, value in worst.items()))
    print(f"burst: anchor=({anchor_x:.3f}, {anchor_y:.3f}) start_zoom={start_zoom:.5f} "
          f"first_frame={burst_index} footprint={footprint} flight_cluster={cluster} "
          f"dirty={dirty} ({dirty_area} px)")
    print(f"33 ms rhythm: {max_step:.1f} px largest screen step")
    print(f"burst fit zooms: {', '.join(f'{e}={z:.4f}' for e, z in zip(EFFECTS, fits))}")

    report = {
        "passed": not failures,
        "failures": failures,
        "source_dir": str(source_dir),
        "compiler": command,
        "worst_deviation": worst,
        "tolerance": TOLERANCE,
        "samples": table,
        "burst": {
            "anchor_x": anchor_x,
            "anchor_y": anchor_y,
            "start_zoom": start_zoom,
            "first_frame": burst_index,
            "footprint": footprint,
            "flight_cluster": cluster,
            "dirty": dirty,
            "dirty_pixels": dirty_area,
        },
        "rhythm_33ms": {"largest_screen_step": round(max_step, 3)},
        "burst_fit_zoom": dict(zip(EFFECTS, fits)),
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"report: {args.report}")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print("PASS: firmware camera matches the approved tracking shot")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
