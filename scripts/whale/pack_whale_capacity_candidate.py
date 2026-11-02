"""Pack an OFFLINE full-library capacity candidate. No device access.

Input GIF dimensions differ from the validated firmware. Do not flash this
output with the current display geometry. Visual and hardware tests pending.
"""
import argparse
import hashlib
import json
import pathlib
import struct


def unpack(blob):
    count, checksum, length = struct.unpack_from('<III', blob)
    assert count < 1000 and length + 12 == len(blob)
    assert sum(blob[12:]) & 0xffff == checksum
    files = {}
    for i in range(count):
        name, size, offset, width, height = struct.unpack_from('<32sIIHH', blob, 12+i*44)
        start = 12 + count*44 + offset
        assert blob[start:start+2] == b'ZZ' and start+size+2 <= len(blob)
        key = name.rstrip(b'\0').decode('ascii')
        assert key not in files
        files[key] = (blob[start+2:start+2+size], width, height)
    return files


def pack(files):
    table, payload = bytearray(), bytearray()
    for name, (data, width, height) in sorted(files.items()):
        assert len(name.encode('ascii')) < 32
        table += struct.pack('<32sIIHH', name.encode('ascii'), len(data), len(payload), width, height)
        payload += b'ZZ' + data
    body = table + payload
    return struct.pack('<III', len(files), sum(body) & 0xffff, len(body)) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=pathlib.Path, required=True)
    parser.add_argument('--candidates', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--pixels', type=int, default=96)
    args = parser.parse_args()
    baseline = unpack((args.repo / 'whale/assets.bin').read_bytes())
    files = {k: v for k, v in baseline.items() if not k.endswith('.gif')}
    library = json.loads((args.repo / 'whale/library.json').read_text(encoding='utf-8'))
    results = json.loads((args.candidates / 'results.json').read_text(encoding='utf-8'))['rows']
    selected = {r['source']: r for r in results if r['pixels'] == args.pixels}
    entries = []
    assert len(selected) == library['count'] == 50
    for item in library['files']:
        source = args.repo / 'whale/source' / item['name']
        assert hashlib.sha256(source.read_bytes()).hexdigest() == item['sha256']
        record = selected[item['name']]
        assert record['fps'] == 6 and record['colors'] == 32, 'This candidate profile requires 6fps/32 colors'
        data = (args.candidates / record['file']).read_bytes()
        assert len(data) == record['bytes'] and data[:6] in (b'GIF87a', b'GIF89a')
        dimensions = struct.unpack_from('<HH', data, 6)
        assert dimensions == (args.pixels, args.pixels)
        assert record['transparent_frames'] == record['frames']
        key = item['id'].lower() + '.gif'
        files[key] = (data, *dimensions)
        entries.append(dict(id=item['id'], source=item['name'], file=key,
                            bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    index = json.loads(baseline['index.json'][0].rstrip(b'\0'))
    aliases = {'neutral.gif': 'w024.gif', 'thinking.gif': 'w019.gif', 'happy.gif': 'w031.gif'}
    for entry in index['emoji_collection']:
        entry['file'] = aliases.get(entry['file'], entry['file'])
    index['emoji_collection'] += [dict(name=e['id'].lower(), file=e['file']) for e in entries]
    files['index.json'] = (json.dumps(index, separators=(',', ':')).encode(), 0, 0)
    assert all(e['file'] in files for e in index['emoji_collection'])
    candidate = pack(files)
    capacity = 0xa00000
    assert len(candidate) <= capacity * 0.9, 'Candidate exceeds the 10% headroom budget'
    assert unpack(candidate) == files, 'Roundtrip changed resource bytes or dimensions'
    for name, original in baseline.items():
        if not name.endswith('.gif') and name != 'index.json':
            assert files[name] == original, f'Preserved resource changed: {name}'
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'EXPERIMENTAL-NOT-INSTALLED-assets.bin').write_bytes(candidate)
    report = dict(status='OFFLINE CAPACITY CANDIDATE; NOT INSTALLED; NOT VISUALLY ACCEPTED',
                  source_commit=library['commit'], animation_count=len(entries),
                  pixels=args.pixels, fps=6, colors=32, bytes=len(candidate), capacity=capacity,
                  remaining_bytes=capacity-len(candidate), free_percent=round(100*(capacity-len(candidate))/capacity, 2),
                  sha256=hashlib.sha256(candidate).hexdigest(),
                  validated='50 source SHA256 hashes; GIF headers/dimensions; resource roundtrip; unchanged fonts/models/PNG',
                  pending='Display scale/geometry adjustment, complete visual review, device playback/voice/interaction tests',
                  entries=entries)
    (args.output / 'manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k != 'entries'}, indent=2))


if __name__ == '__main__':
    main()
