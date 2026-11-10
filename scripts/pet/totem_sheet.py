from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
raw = (ROOT / "local-only" / "totem-preview" / "frames60.rgb").read_bytes()
size = 240
fps = 60
times = [0.00, 0.08, 0.16, 0.24, 0.32, 0.40, 0.50, 0.70, 0.95, 1.10,
         1.35, 1.60, 1.78, 1.82, 1.86, 1.92, 2.00, 2.10, 2.20, 2.28,
         2.40, 2.55, 2.80, 2.95, 3.05, 3.20, 3.35, 3.45]
cols = 7
rows = (len(times) + cols - 1) // cols
thumb = 200
sheet = Image.new("RGB", (cols * thumb, rows * (thumb + 18)), (18, 18, 18))
draw = ImageDraw.Draw(sheet)
for i, t in enumerate(times):
    frame = min(int(round(t * fps)), len(raw) // (size * size * 3) - 1)
    chunk = raw[frame * size * size * 3:(frame + 1) * size * size * 3]
    image = Image.frombytes("RGB", (size, size), chunk).resize((thumb, thumb), Image.NEAREST)
    x = (i % cols) * thumb
    y = (i // cols) * (thumb + 18)
    sheet.paste(image, (x, y))
    draw.text((x + 4, y + thumb + 3), f"{t:.2f}s", fill=(230, 230, 230))
out = ROOT / "local-only" / "totem-preview" / "sheet.png"
sheet.save(out)
print(out, sheet.size)
