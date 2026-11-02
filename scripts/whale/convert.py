"""Convert one of the three current actions with an installed FFmpeg."""
import argparse
import pathlib
import shutil
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
names = {'neutral': '待机呼吸休闲.webm', 'thinking': '工作思考.webm', 'happy': '点击回应 - 开心跃动.webm'}
parser = argparse.ArgumentParser()
parser.add_argument('--action', choices=names, required=True)
parser.add_argument('--fps', type=int, default=10)
parser.add_argument('--colors', type=int, default=128)
args = parser.parse_args()
if not 1 <= args.fps <= 24 or not 4 <= args.colors <= 256:
    parser.error('fps must be 1..24 and colors 4..256')
ffmpeg = shutil.which('ffmpeg')
if not ffmpeg:
    parser.error('Install FFmpeg with libvpx-vp9 support first')
filter_graph = f'fps={args.fps},scale=224:224:flags=lanczos,split[a][b];[a]palettegen=max_colors={args.colors}:reserve_transparent=1:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle'
subprocess.run([ffmpeg, '-v', 'error', '-y', '-c:v', 'libvpx-vp9', '-i', str(root / 'whale/source' / names[args.action]), '-filter_complex', filter_graph, '-an', '-loop', '0', str(root / f'whale/animations/{args.action}.gif')], check=True)
print('GIF converted. Rebuild the assets package and validate appearance before use.')
