"""Measure source resolution tradeoffs without changing the on-screen scale.

Requires FFmpeg with libvpx-vp9 and Pillow. Writes only to --output.
This produces GIF candidates, not installed firmware or hardware validation.
"""
import argparse
import concurrent.futures
import json
import pathlib
import shutil
import subprocess

from PIL import Image


def encode(job):
    source, output, size, colors, fps = job
    dest = output / f'{source.stem}-{size}px-{fps}fps-{colors}colors.gif'
    graph = (f'fps={fps},scale={size}:{size}:flags=lanczos,split[a][b];'
             f'[a]palettegen=max_colors={colors}:reserve_transparent=1:stats_mode=full[p];'
             '[b][p]paletteuse=dither=none:diff_mode=rectangle')
    subprocess.run([shutil.which('ffmpeg'), '-v', 'error', '-y', '-c:v', 'libvpx-vp9',
                    '-i', str(source), '-filter_complex', graph, '-an', '-loop', '0', str(dest)],
                   check=True, capture_output=True)
    with Image.open(dest) as image:
        duration = transparent = duplicates = 0
        previous = None
        for i in range(image.n_frames):
            image.seek(i)
            image.load()
            duration += image.info.get('duration', 0)
            frame = image.convert('RGBA')
            transparent += frame.getchannel('A').getextrema()[0] < 255
            values = frame.tobytes()
            duplicates += values == previous
            previous = values
        return dict(source=source.name, file=dest.name, pixels=size, colors=colors, fps=fps,
                    bytes=dest.stat().st_size, frames=image.n_frames, duration_ms=duration,
                    transparent_frames=transparent, identical_adjacent_frames=duplicates,
                    device_tested=False, visual_accepted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--sizes', nargs='+', type=int, default=[160, 128, 112, 96])
    parser.add_argument('--colors', type=int, default=64)
    parser.add_argument('--fps', type=int, default=8)
    parser.add_argument('--all', action='store_true', help='Measure every WebM, not just three samples')
    parser.add_argument('--ids', nargs='+', help='Select IDs from --library, e.g. W001 W024')
    parser.add_argument('--library', type=pathlib.Path, help='whale/library.json, required with --ids')
    args = parser.parse_args()
    if not shutil.which('ffmpeg'):
        parser.error('ffmpeg is required')
    if args.all and args.ids:
        parser.error('--all and --ids are mutually exclusive')
    if args.ids and not args.library:
        parser.error('--ids requires --library')
    if not (2 <= args.colors <= 256 and 1 <= args.fps <= 60 and all(16 <= s <= 1024 for s in args.sizes)):
        parser.error('Invalid colors, fps, or sizes')
    args.output.mkdir(parents=True, exist_ok=True)
    names = ['待机呼吸休闲.webm', '工作思考.webm', '点击回应 - 开心跃动.webm']
    if args.ids:
        library = json.loads(args.library.read_text(encoding='utf-8'))
        entries = {entry['id']: entry['name'] for entry in library['files']}
        if set(args.ids) - entries.keys():
            parser.error('Unknown animation ID')
        sources = [args.source / entries[key] for key in args.ids]
    else:
        sources = sorted(args.source.glob('*.webm')) if args.all else [args.source / n for n in names]
    if not sources or any(not source.is_file() for source in sources):
        parser.error('Source files are missing')
    jobs = [(s, args.output, size, args.colors, args.fps) for s in sources for size in args.sizes]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        rows = list(pool.map(encode, jobs))
    report = dict(note='Offline measurements; source resolution changes sharpness, not display size. '
                       'No firmware transform is changed by this script.', rows=rows)
    (args.output / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=True, indent=2))
