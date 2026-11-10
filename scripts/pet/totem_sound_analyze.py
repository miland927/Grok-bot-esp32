from pathlib import Path
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
wav = ROOT / "local-only" / "totem-reference" / "use_totem.wav"
with wave.open(str(wav), "rb") as h:
    sr = h.getframerate()
    ch = h.getnchannels()
    data = np.frombuffer(h.readframes(h.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
print("sr", sr, "ch", ch, "n", len(data), "dur", len(data) / sr, "peak", float(np.max(np.abs(data))))
# RMS envelope per 100ms
win = int(sr * 0.1)
for i in range(0, len(data), win):
    chunk = data[i:i + win]
    if chunk.size == 0:
        break
    db = 20 * np.log10(max(float(np.sqrt(np.mean(chunk ** 2))), 1e-9))
    print(f"{i / sr:5.2f}s {db:6.1f} dB")
# onset: first sample above 0.05, peak time
idx = np.nonzero(np.abs(data) > 0.05)[0]
print("first>0.05", idx[0] / sr if len(idx) else None)
print("peak at", int(np.argmax(np.abs(data))) / sr)
# spectral centroid in 0.2s windows
for start in (0.0, 0.2, 0.5, 1.0, 2.0, 3.0):
    a = int(start * sr)
    b = min(len(data), a + int(0.2 * sr))
    chunk = data[a:b]
    spec = np.abs(np.fft.rfft(chunk * np.hanning(len(chunk))))
    freq = np.fft.rfftfreq(len(chunk), 1 / sr)
    print(f"centroid {start:.1f}s", float(np.sum(freq * spec) / max(np.sum(spec), 1e-9)))
