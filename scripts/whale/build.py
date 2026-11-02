"""Build the whale variant using the active ESP-IDF environment."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
result = subprocess.run([
    sys.executable, str(root / 'scripts/build.py'),
    'waveshare/esp32-s3-touch-amoled-2.06',
    '--name', 'esp32-s3-touch-amoled-2.06-whale', '--language', 'zh-CN',
], cwd=root)
raise SystemExit(result.returncode)
