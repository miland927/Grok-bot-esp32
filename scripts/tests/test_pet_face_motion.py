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

    #include "pet_face_motion.h"

    static int failures = 0;

    static void Check(bool condition, const char* what, float actual, float expected) {
        if (condition) {
            return;
        }
        std::printf("FAIL %s: actual %.4f expected %.4f\n", what, actual, expected);
        ++failures;
    }

    static void Spring(float& pos, float& vel, float target, float dt, float k, float d) {
        const float acc = (target - pos) * k - vel * d;
        vel += acc * dt;
        pos += vel * dt;
    }

    int main() {
        constexpr float dt = 0.05f;

        // Drag release from the full squash target: one clear overshoot, one
        // smaller reversal, then settle.
        float pos = -0.30f;
        float vel = 0.0f;
        float overshoot = 0.0f;
        float settle = 0.0f;
        int signs = 0;
        int last_sign = 0;
        for (int i = 0; i < 24; ++i) {
            Spring(pos, vel, 0.0f, dt, pet_face::kSquashStiffness, pet_face::kSquashDamping);
            if (pos > overshoot) overshoot = pos;
            if (std::fabs(pos) > 0.01f) settle = (i + 1) * dt;
            const int sign = pos > 0.005f ? 1 : (pos < -0.005f ? -1 : 0);
            if (sign != 0 && last_sign != 0 && sign != last_sign) ++signs;
            if (sign != 0) last_sign = sign;
        }
        Check(overshoot > 0.06f && overshoot < 0.15f, "squash overshoot", overshoot, 0.09f);
        Check(signs >= 3, "squash oscillations", signs, 3);
        Check(settle > 0.0f && settle < 0.8f, "squash settle", settle, 0.5f);

        // The old spring was effectively non-bouncy; keep a regression check.
        float old_pos = -0.30f;
        float old_vel = 0.0f;
        float old_peak = 0.0f;
        for (int i = 0; i < 24; ++i) {
            Spring(old_pos, old_vel, 0.0f, dt, 105.0f, 15.0f);
            if (old_pos > old_peak) old_peak = old_pos;
        }
        Check(old_peak < 0.02f, "old spring is stiff", old_peak, 0.0f);

        // A short press impulse should produce a visible but not violent squash.
        pos = 0.0f;
        vel = -2.5f;
        float press_peak = 0.0f;
        for (int i = 0; i < 16; ++i) {
            Spring(pos, vel, 0.0f, dt, pet_face::kSquashStiffness, pet_face::kSquashDamping);
            if (pos < press_peak) press_peak = pos;
        }
        Check(press_peak < -0.05f && press_peak > -0.15f, "press squash", press_peak, -0.09f);

        // The eye lag follows the hop spring and stays a few pixels.
        float hop = 16.0f;
        float hop_v = 0.0f;
        float eye = 16.0f;
        float eye_v = 0.0f;
        float lag_peak = 0.0f;
        for (int i = 0; i < 24; ++i) {
            Spring(hop, hop_v, 0.0f, dt, 60.0f, 8.5f);
            Spring(eye, eye_v, hop, dt, pet_face::kEyeLagStiffness, pet_face::kEyeLagDamping);
            float lag = (eye - hop) * pet_face::kEyeLagScale;
            if (lag > pet_face::kEyeLagClamp) lag = pet_face::kEyeLagClamp;
            if (lag < -pet_face::kEyeLagClamp) lag = -pet_face::kEyeLagClamp;
            if (std::fabs(lag) > lag_peak) lag_peak = std::fabs(lag);
        }
        Check(lag_peak > 1.0f && lag_peak < 4.5f, "eye lag", lag_peak, 3.0f);

        if (failures != 0) {
            std::printf("%d failure(s)\n", failures);
            return 1;
        }
        std::printf("face motion ok: overshoot=%.3f signs=%d settle=%.2fs press=%.3f "
                    "eyelag=%.2fpx\n",
                    overshoot, signs, settle, press_peak, lag_peak);
        return 0;
    }
"""


class PetFaceMotionTest(unittest.TestCase):
    def test_jelly_spring_overshoot_and_eye_lag(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or shutil.which(compiler[0]) is None:
            self.skipTest("No host C++ compiler for the face motion test")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            build = Path(directory)
            source = build / "face_motion_test.cc"
            source.write_text(textwrap.dedent(DRIVER), encoding="utf-8")
            executable = build / "face_motion_test"
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
