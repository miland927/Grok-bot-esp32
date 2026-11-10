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

    #include "pet_face_geometry.h"

    using pet_face::ConstrainVerticalRadius;

    static int failures = 0;

    static void Check(bool condition, const char* what, float actual, float expected) {
        if (condition) {
            return;
        }
        std::printf("FAIL %s: actual %.4f expected %.4f\n", what, actual, expected);
        ++failures;
    }

    static void Close(float actual, float expected, float eps, const char* what) {
        Check(std::fabs(actual - expected) <= eps, what, actual, expected);
    }

    int main() {
        // The resting face is untouched.
        Close(ConstrainVerticalRadius(120.0f, 88.0f), 88.0f, 1e-4f, "rest radius");

        // Full pull: center is kept, the vertical stretch is reduced enough
        // that the anti-aliased bottom stays two pixels above the canvas edge.
        const float full = ConstrainVerticalRadius(136.0f, 114.0f);
        const float full_bottom = 136.0f + pet_face::kAntiAliasExtent * full;
        Check(full_bottom <= 237.01f, "full pull bottom", full_bottom, 237.0f);
        Check(full > 88.0f, "full pull still taller than rest", full, 88.0f);
        Check(full < 114.0f, "full pull constrained", full, 114.0f);

        // Spring overshoot while dragging down.
        const float deep = ConstrainVerticalRadius(156.0f, 114.0f);
        const float deep_bottom = 156.0f + pet_face::kAntiAliasExtent * deep;
        Check(deep_bottom <= 237.01f, "deep pull bottom", deep_bottom, 237.0f);

        // Bounce above the rest position must not clip the top.
        const float up = ConstrainVerticalRadius(84.0f, 88.0f);
        const float up_top = 84.0f - pet_face::kAntiAliasExtent * up;
        Check(up_top >= 1.99f, "upward top", up_top, 2.0f);

        // Scan the complete spring/tick envelope used by the renderer.
        for (float cy = 84.0f; cy <= 160.0f; cy += 0.25f) {
            for (float ry = 40.0f; ry <= 120.0f; ry += 0.25f) {
                const float c = ConstrainVerticalRadius(cy, ry);
                const float bottom = cy + pet_face::kAntiAliasExtent * c;
                const float top = cy - pet_face::kAntiAliasExtent * c;
                if (bottom > 237.01f || top < 1.99f) {
                    std::printf("FAIL envelope cy=%.2f ry=%.2f top=%.2f bottom=%.2f\n", cy, ry,
                                top, bottom);
                    ++failures;
                }
            }
        }

        // The constraint must remain continuous as the spring moves.
        float previous = ConstrainVerticalRadius(120.0f, 114.0f);
        for (float cy = 120.0f; cy <= 160.0f; cy += 0.05f) {
            const float value = ConstrainVerticalRadius(cy, 114.0f);
            if (std::fabs(value - previous) > 0.3f) {
                std::printf("FAIL discontinuity at cy=%.2f: %.4f -> %.4f\n", cy, previous, value);
                ++failures;
                break;
            }
            previous = value;
        }

        if (failures != 0) {
            std::printf("%d failure(s)\n", failures);
            return 1;
        }
        std::printf("face geometry ok: full=%.2f bottom=%.2f deep=%.2f deep_bottom=%.2f "
                    "up=%.2f top=%.2f\n",
                    full, full_bottom, deep, deep_bottom, up, up_top);
        return 0;
    }
"""


class PetFaceGeometryTest(unittest.TestCase):
    def test_face_stays_inside_canvas_with_spring_overshoot(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or shutil.which(compiler[0]) is None:
            self.skipTest("No host C++ compiler for the pure face geometry test")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            build = Path(directory)
            source = build / "face_geometry_test.cc"
            source.write_text(textwrap.dedent(DRIVER), encoding="utf-8")
            executable = build / "face_geometry_test"
            command = compiler + [
                "-std=c++20",
                f"-I{DISPLAY}",
                str(source),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=build)
            subprocess.run([str(executable)], check=True, cwd=build)


if __name__ == "__main__":
    unittest.main()
