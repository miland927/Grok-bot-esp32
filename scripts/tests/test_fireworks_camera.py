import os
import shlex
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DISPLAY = ROOT / "main" / "display"

DRIVER = r"""
    #include <cmath>
    #include <cstdio>

    #include "pet_fireworks_camera.h"
    #include "pet_fireworks_layout.h"
    #include "pet_firework_selection.h"

    using pet_fireworks::Camera;

    static int failures = 0;

    static void Check(bool condition, const char* what, float actual, float expected) {
        if (condition) {
            return;
        }
        std::printf("FAIL %s: actual %.6f expected %.6f\n", what, actual, expected);
        ++failures;
    }

    static void Close(float actual, float expected, float eps, const char* what) {
        Check(std::fabs(actual - expected) <= eps, what, actual, expected);
    }

    int main() {
        PetMedia::FireworkSelection selection;
        std::array<int, PetMedia::kFireworkNames.size()> drawn{};
        for (unsigned roll = 0; roll < 100; ++roll) {
            ++drawn[selection.Pick(roll)];
        }
        if (drawn != std::array{10, 14, 14, 14, 10, 14, 10, 14}) {
            std::puts("FAIL firework weights");
            return 1;
        }
        selection.test_effect = 6;
        if (selection.Pick(0) != 6 || selection.Pick(99) != 6) {
            std::puts("FAIL named firework override");
            return 1;
        }
        Camera camera;
        camera.Prepare(410, 502);
        if (!camera.ready()) {
            std::puts("FAIL camera not prepared");
            return 1;
        }

        // Golden rows from the approved v2 410x502 preview (60 fps sample dump).
        struct Sample {
            float ms, origin_x, origin_y, zoom, screen_x, screen_y, height;
        };
        const Sample samples[] = {
            {0, -111.00f, -179.00f, 4.000f, 205.00f, 281.00f, 177.60f},
            {603, 4.71f, -25.61f, 2.488f, 209.09f, 200.64f, 92.44f},
            {748, 8.19f, 34.84f, 2.386f, 206.13f, 229.20f, 73.75f},
            {827, 8.58f, 39.96f, 2.364f, 205.08f, 227.31f, 69.83f},
            {912, 10.02f, 56.07f, 2.339f, 204.89f, 235.49f, 65.47f},
            {960, 11.10f, 54.66f, 2.324f, 205.02f, 229.47f, 62.97f},
            {1020, 11.81f, 55.76f, 2.315f, 205.31f, 225.34f, 60.03f},
            {1400, 11.81f, 55.76f, 2.315f, 208.55f, 180.75f, 27.91f},
        };
        for (const auto& s : samples) {
            const auto view = camera.LensAt(s.ms);
            const auto rocket = camera.RocketAt(s.ms);
            const auto screen = Camera::Project(rocket.x, rocket.y, view);
            Close(view.origin_x, s.origin_x, 0.06f, "origin_x");
            Close(view.origin_y, s.origin_y, 0.06f, "origin_y");
            Close(view.zoom, s.zoom, 0.002f, "zoom");
            Close(screen.x, s.screen_x, 0.06f, "screen_x");
            Close(screen.y, s.screen_y, 0.06f, "screen_y");
            Close(37.0f * camera.ScaleAt(s.ms) * view.zoom, s.height, 0.10f, "height");
        }

        Close(camera.OffsetAt(603), -12.0f, 1e-3f, "offset 603");
        Close(camera.OffsetAt(748), 6.5f, 1e-3f, "offset 748");
        Close(camera.OffsetAt(827), -3.4f, 1e-3f, "offset 827");
        Close(camera.OffsetAt(912), 3.8f, 1e-3f, "offset 912");
        Close(camera.OffsetAt(960), 0.0f, 1e-3f, "offset 960");

        // The lens freezes at 1020 ms and stays fixed through the burst.
        const auto locked = camera.LensAt(1020);
        for (float ms = 1020; ms <= 1400; ms += 1.0f) {
            const auto view = camera.LensAt(ms);
            if (view.origin_x != locked.origin_x || view.origin_y != locked.origin_y ||
                view.zoom != locked.zoom) {
                std::printf("FAIL locked lens moved at %.0f ms\n", ms);
                ++failures;
                break;
            }
        }

        // The rocket never falls while the show climbs to its apex.
        float previous = 1000.0f;
        for (float ms = 0; ms <= 1400; ms += 1.0f) {
            const float y = camera.RocketAt(ms).y;
            if (y > previous + 1e-4f) {
                std::printf("FAIL rocket y increased at %.0f ms\n", ms);
                ++failures;
                break;
            }
            previous = y;
        }

        // Burst keeps the frozen source point pinned for every expansion zoom.
        const auto anchor = camera.BurstAnchor();
        Close(anchor.x, 208.5513f, 0.02f, "burst anchor x");
        Close(anchor.y, 180.7460f, 0.02f, "burst anchor y");
        for (float zoom : {2.3146f, 2.8f, 3.1f, 3.3938f}) {
            const auto view = camera.BurstView(zoom);
            const auto point = Camera::Project(85, 54, view);
            Close(point.x, anchor.x, 1e-3f, "burst anchor pinned x");
            Close(point.y, anchor.y, 1e-3f, "burst anchor pinned y");
        }
        Close(camera.BurstStartZoom(), 2.314586f, 1e-4f, "burst start zoom");
        Close(camera.BurstZoomAt(1400, 4.7f, 3.3938f), 2.314586f, 1e-4f, "burst zoom start");
        Close(camera.BurstZoomAt(1650, 4.7f, 3.3938f), 2.854212f, 1e-4f, "burst zoom middle");
        Close(camera.BurstZoomAt(1900, 4.7f, 3.3938f), 3.3938f, 1e-4f, "burst zoom end");
        Close(camera.BurstZoomAt(1900, 4.7f, 2.9761f), 2.9761f, 1e-4f, "burst material cap");

        // Sampling is stateless: order and skipped ticks cannot change a value.
        float first[44];
        for (int i = 0; i < 44; ++i) {
            first[i] = camera.LensAt(static_cast<float>(i) * 33.0f).origin_y;
        }
        for (int i = 43; i >= 0; --i) {
            camera.LensAt(static_cast<float>(i) * 16.5f);
        }
        for (int i = 0; i < 44; ++i) {
            const float again = camera.LensAt(static_cast<float>(i) * 33.0f).origin_y;
            if (again != first[i]) {
                std::printf("FAIL repeated sample changed at %d\n", i);
                ++failures;
                break;
            }
        }

        pet_fireworks::Rect a{1, 2, 3, 4};
        pet_fireworks::Rect b{5, 6, 7, 8};
        const auto united = pet_fireworks::Union(a, b);
        Check(united.x1 == 1 && united.y1 == 2 && united.x2 == 7 && united.y2 == 8,
              "union", 0, 0);
        const auto clipped = pet_fireworks::Clip({-2, -2, 5, 5}, 4, 4);
        Check(clipped.x1 == 0 && clipped.y1 == 0 && clipped.x2 == 3 && clipped.y2 == 3,
              "clip", 0, 0);
        Check(pet_fireworks::Empty(pet_fireworks::Clip({10, 10, 20, 20}, 4, 4)), "empty clip",
              0, 0);

        if (failures != 0) {
            std::printf("%d failure(s)\n", failures);
            return 1;
        }
        std::puts("fireworks camera ok");
        return 0;
    }
"""


class FireworksCameraTest(unittest.TestCase):
    def test_camera_matches_approved_tracking_shot(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or shutil.which(compiler[0]) is None:
            self.skipTest("No host C++ compiler for the pure fireworks camera test")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            build = Path(directory)
            source = build / "fireworks_camera_test.cc"
            source.write_text(textwrap.dedent(DRIVER), encoding="utf-8")
            executable = build / "fireworks_camera_test"
            command = compiler + [
                "-std=c++20",
                f"-I{DISPLAY}",
                str(source),
                str(DISPLAY / "pet_fireworks_camera.cc"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=build)
            subprocess.run([str(executable)], check=True, cwd=build)


if __name__ == "__main__":
    unittest.main()
