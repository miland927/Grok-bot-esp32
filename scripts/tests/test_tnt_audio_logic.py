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
    #include <cstdio>

    #include "pet_tnt_audio_logic.h"

    namespace audio = pet_tnt_audio;
    static int failures = 0;

    static void Check(bool condition, const char* what) {
        if (!condition) {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    int main() {
        using audio::Reject;
        Check(audio::Decide(true, false, true, true, 0) == Reject::kNone, "fresh request");
        Check(audio::Decide(false, false, true, true, 0) == Reject::kMissing, "missing asset");
        Check(audio::Decide(true, true, true, true, 0) == Reject::kBusy, "busy track");
        Check(audio::Decide(true, false, false, true, 0) == Reject::kPlaybackBusy,
              "playback busy");
        Check(audio::Decide(true, false, true, false, 0) == Reject::kNotIdle, "not idle");
        Check(audio::Decide(true, false, true, true, 150000) == Reject::kLate, "past in-point");
        Check(audio::Decide(true, false, true, true, 90000) == Reject::kNone, "inside in-point");

        audio::Gate gate;
        Check(gate.Begin(1), "first begin");
        Check(!gate.Begin(1), "duplicate begin");
        Check(!gate.Begin(2), "second show while running");
        Check(gate.Continue(1), "first continue");
        Check(!gate.Continue(2), "wrong show cannot continue");
        gate.Cancel(1);
        Check(!gate.Continue(1), "cancelled show stops");
        Check(gate.Running(), "worker still owns the slot");
        Check(!gate.Begin(2), "new show waits for the old worker");
        gate.Finish(1);
        Check(!gate.Running(), "worker released the slot");
        Check(gate.Begin(2), "new show after finish");
        Check(gate.Continue(2), "new show continues");
        Check(!gate.Continue(1), "old show never replays");
        gate.Cancel(1);
        Check(gate.Continue(2), "old cancel does not stop the new show");
        gate.Cancel(2);
        Check(!gate.Continue(2), "new show cancelled");
        gate.Finish(2);
        Check(gate.Begin(3), "third show after cancellation");
        Check(gate.Continue(3), "third show continues");

        if (failures != 0) {
            std::printf("%d failure(s)\n", failures);
            return 1;
        }
        std::puts("tnt audio logic ok");
        return 0;
    }
"""


class TntAudioLogicTest(unittest.TestCase):
    def test_single_start_busy_cancel_and_old_track(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or shutil.which(compiler[0]) is None:
            self.skipTest("No host C++ compiler for the TNT audio logic test")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            build = Path(directory)
            source = build / "tnt_audio_logic_test.cc"
            source.write_text(textwrap.dedent(DRIVER), encoding="utf-8")
            executable = build / "tnt_audio_logic_test"
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
