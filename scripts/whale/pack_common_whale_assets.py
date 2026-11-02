"""Build the provisional 160px common-ten resource pack; never accesses hardware."""
import argparse
import hashlib
import json
import pathlib
import struct

from pack_whale_capacity_candidate import pack, unpack

IDS = ['W001', 'W006', 'W019', 'W024', 'W027', 'W029', 'W030', 'W031', 'W035', 'W043']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=pathlib.Path, required=True)
    parser.add_argument('--candidates', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    baseline = unpack((args.repo/'whale/assets.bin').read_bytes())
    files = {key: value for key, value in baseline.items() if not key.endswith('.gif')}
    library = json.loads((args.repo/'whale/library.json').read_text(encoding='utf-8'))
    rows = json.loads((args.candidates/'results.json').read_text(encoding='utf-8'))['rows']
    selected = {r['source']: r for r in rows if r['pixels'] == 160 and r['fps'] == 8 and r['colors'] == 64}
    entries = []
    for item in library['files']:
        if item['id'] not in IDS:
            continue
        assert hashlib.sha256((args.repo/'whale/source'/item['name']).read_bytes()).hexdigest() == item['sha256']
        row = selected[item['name']]
        data = (args.candidates/row['file']).read_bytes()
        assert len(data) == row['bytes'] and data[:6] in (b'GIF87a', b'GIF89a')
        assert struct.unpack_from('<HH', data, 6) == (160, 160)
        assert row['frames'] == row['transparent_frames'] == 80
        assert 9950 <= row['duration_ms'] <= 10100
        key = item['id'].lower()+'.gif'
        files[key] = (data, 160, 160)
        entries.append(dict(id=item['id'], source=item['name'], file=key, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    assert [e['id'] for e in entries] == IDS
    index = json.loads(baseline['index.json'][0].rstrip(b'\0'))
    # Preserve legacy aliases; board-local behavior also selects wXXX keys directly.
    aliases = {'neutral.gif': 'w024.gif', 'thinking.gif': 'w019.gif', 'happy.gif': 'w031.gif'}
    collection = [e for e in index['emoji_collection'] if e['name'].upper() not in IDS]
    for entry in collection:
        entry['file'] = aliases.get(entry['file'], entry['file'])
        assert entry['file'] in files
    collection += [dict(name=e['id'].lower(), file=e['file']) for e in entries]
    index['emoji_collection'] = collection
    files['index.json'] = (json.dumps(index, separators=(',', ':')).encode(), 0, 0)
    result = pack(files)
    capacity = 0xa00000
    assert len(result) <= capacity*0.9
    assert unpack(result) == files
    for key, value in baseline.items():
        if not key.endswith('.gif') and key != 'index.json':
            assert files[key] == value
    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output/'assets-common-ten-160px.bin'
    if target.exists():
        assert target.read_bytes() == result, 'Existing candidate differs; use a new output directory'
    else:
        target.write_bytes(result)
    manifest = dict(status='OFFLINE resource output; installation and interaction acceptance tracked separately',
                    pixels=160, fps=8, colors=64, count=10, bytes=len(result), capacity=capacity,
                    free_percent=round(100*(capacity-len(result))/capacity, 2),
                    sha256=hashlib.sha256(result).hexdigest(), entries=entries)
    (args.output/'assets-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in manifest.items() if k!='entries'}, indent=2))


if __name__ == '__main__':
    main()
