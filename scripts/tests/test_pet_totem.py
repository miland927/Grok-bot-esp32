import importlib.util
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

AUDIO_DRIVER = r"""
    #include <cstdio>

    #include "pet_totem_audio_logic.h"

    namespace audio = pet_totem_audio;
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
        Check(audio::Decide(true, false, true, true, 400000, audio::kLinkDeadlineUs) ==
                  Reject::kNone,
              "linked start inside the black hold");
        Check(audio::Decide(true, false, false, true, 400000, audio::kLinkDeadlineUs) ==
                  Reject::kPlaybackBusy,
              "TNT tail still queued falls back silently");
        Check(audio::Decide(true, false, true, true, 600000, audio::kLinkDeadlineUs) ==
                  Reject::kLate,
              "linked start past the sync window");
        Check(audio::LinkOffsetMs(1000000, 1000000) == 0, "on-time offset");
        Check(audio::LinkOffsetMs(1400000, 1000000) == 400, "400 ms tail offset");
        Check(audio::LinkOffsetMs(3000000, 1000000) == 450, "offset caps at the sync window");

        audio::Gate gate;
        Check(gate.Begin(11), "first begin");
        Check(!gate.Begin(11), "duplicate begin");
        Check(!gate.Begin(12), "second show while running");
        Check(gate.Continue(11), "first continue");
        Check(!gate.Continue(12), "wrong show cannot continue");
        gate.Cancel(11);
        Check(!gate.Continue(11), "cancelled show stops");
        Check(gate.Running(), "worker still owns the slot");
        Check(!gate.Begin(12), "new show waits for the old worker");
        gate.Finish(11);
        Check(gate.Begin(12), "new show after finish");
        Check(gate.Continue(12), "new show continues");
        Check(!gate.Continue(11), "old show never replays");
        gate.Cancel(11);
        Check(gate.Continue(12), "old cancel does not stop the new show");

        if (failures != 0) {
            std::printf("%d failure(s)\n", failures);
            return 1;
        }
        std::puts("totem audio logic ok");
        return 0;
    }
"""


def compiler():
    command = shlex.split(os.environ.get("CXX", "c++"))
    if not command or shutil.which(command[0]) is None:
        return None
    return command


class TotemTimelineTest(unittest.TestCase):
    def test_stage_timing_pose_and_draw(self):
        cc = compiler()
        if cc is None:
            self.skipTest("No host C++ compiler for the totem timeline test")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            build = Path(directory)
            executable = build / "totem_preview"
            command = cc + [
                "-std=c++20",
                "-O1",
                f"-I{DISPLAY}",
                str(DISPLAY / "pet_totem.cc"),
                str(ROOT / "scripts" / "pet" / "totem_preview.cc"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=build)
            result = subprocess.run([str(executable)], check=True, cwd=build,
                                    capture_output=True, text=True)
            self.assertIn("passed", result.stdout)


class TotemAudioLogicTest(unittest.TestCase):
    def test_single_start_busy_cancel_and_old_track(self):
        cc = compiler()
        if cc is None:
            self.skipTest("No host C++ compiler for the totem audio logic test")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            build = Path(directory)
            source = build / "totem_audio_logic_test.cc"
            source.write_text(textwrap.dedent(AUDIO_DRIVER), encoding="utf-8")
            executable = build / "totem_audio_logic_test"
            subprocess.run(cc + ["-std=c++20", f"-I{DISPLAY}", str(source), "-o",
                                 str(executable)], check=True, cwd=build)
            subprocess.run([str(executable)], check=True, cwd=build)


class TotemPackTest(unittest.TestCase):
    def test_tta1_header_crc_and_audio(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is required for the totem pack test")
        spec = importlib.util.spec_from_file_location(
            "pack_totem_pack", ROOT / "scripts" / "pet" / "pack_totem_pack.py")
        if spec is None or spec.loader is None:
            self.skipTest("totem packer not available")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            build = Path(directory)
            texture = build / "totem.png"
            image = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
            image.putpixel((8, 8), (255, 215, 94, 255))
            image.save(texture)
            audio = build / "track.ogg"
            audio.write_bytes(b"OggS" + b"\0" * 200)
            pack = module.build(texture, audio)
            width, height, audio_offset = module.validate(pack)
            self.assertEqual((width, height), (16, 16))
            self.assertEqual(audio_offset, 16 + 16 * 16 * 2 + 2 * 16)
            self.assertEqual(pack[audio_offset:audio_offset + 4], b"OggS")
            self.assertEqual(len(pack) - audio_offset, 204)


if __name__ == "__main__":
    unittest.main()
