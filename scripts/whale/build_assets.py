"""Repack current common-ten GIFs while preserving fonts/models; supports the legacy three-GIF baseline."""
import argparse
import hashlib
import json
import pathlib
import struct

root = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--check', action='store_true', help='Verify reproducibility without changing files')
args = parser.parse_args()
target = root / 'whale/assets.bin'
blob = target.read_bytes()
count, checksum, length = struct.unpack_from('<III', blob)
assert count < 1000 and length + 12 == len(blob)
assert sum(blob[12:]) & 0xffff == checksum
files = {}
for i in range(count):
    name, size, offset, width, height = struct.unpack_from('<32sIIHH', blob, 12 + i * 44)
    start = 12 + count * 44 + offset
    assert blob[start:start + 2] == b'ZZ' and start + size + 2 <= len(blob)
    key = name.rstrip(b'\0').decode('ascii')
    assert key not in files
    files[key] = (blob[start + 2:start + 2 + size], width, height)
common_ids = ('w001', 'w006', 'w019', 'w024', 'w027', 'w029', 'w030', 'w031', 'w035', 'w043')
is_common_pack = all(key + '.gif' in files for key in common_ids)
names = common_ids if is_common_pack else ('neutral', 'thinking', 'happy')
animation_dir = root / ('whale/animations/common' if is_common_pack else 'whale/animations')
expected_size = (160, 160) if is_common_pack else (224, 224)
for name in names:
    data = (animation_dir / (name + '.gif')).read_bytes()
    assert data[:6] in (b'GIF87a', b'GIF89a')
    width, height = struct.unpack_from('<HH', data, 6)
    assert (width, height) == expected_size, 'Adjust display geometry before changing GIF canvas dimensions'
    files[name + '.gif'] = (data, width, height)
table, payload = bytearray(), bytearray()
for name, (data, width, height) in sorted(files.items()):
    assert len(name.encode('ascii')) < 32
    table += struct.pack('<32sIIHH', name.encode('ascii'), len(data), len(payload), width, height)
    payload += b'ZZ' + data
body = table + payload
packed = struct.pack('<III', len(files), sum(body) & 0xffff, len(body)) + body
assert len(packed) <= 0xa00000, 'Assets exceed the 10MiB partition'
if args.check:
    assert packed == blob, 'GIFs differ from the packed resource; run build_assets.py to rebuild'
else:
    target.write_bytes(packed)
    index = json.loads(files['index.json'][0].rstrip(b'\0'))
    manifest = dict(size=len(packed), capacity=0xa00000, sha256=hashlib.sha256(packed).hexdigest(), files={key:len(value[0]) for key,value in files.items()}, emoji_collection=index['emoji_collection'])
    (root / 'whale/assets-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f'Assets verified: {len(packed):,} bytes; sha256={hashlib.sha256(packed).hexdigest()}')
