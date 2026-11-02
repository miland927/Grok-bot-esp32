import importlib.util
import io
import json
from pathlib import Path
import random
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/whale'))
from lossless_gif import decode,encode,optimize
from resource_audit import inspect_resources
from resource_manager import unpack
from workflow_core import sha


class CapacityTests(unittest.TestCase):
    def test_byte_accounting_reconciles_and_does_not_invent_runtime_usage(self):
        blob=(ROOT/'whale/assets.bin').read_bytes()
        audit=inspect_resources(blob,10*1048576)
        self.assertEqual(sum(x['bytes'] for x in audit['categories'].values())+audit['container_overhead_bytes'],len(blob))
        self.assertEqual(audit['categories']['animations']['count'],10)
        self.assertEqual(audit['categories']['fonts']['bytes'],2609092)
        self.assertIsNone(audit['runtime_cpu_percent'])
        self.assertIsNone(audit['runtime_ram_bytes'])
        self.assertIsNone(audit['actual_render_fps'])
        self.assertLess(audit['resource_share_of_physical_flash_percent'],audit['used_percent'])

    def test_aliases_do_not_count_as_duplicate_payloads(self):
        blob=(ROOT/'whale/assets.bin').read_bytes()
        audit=inspect_resources(blob,10*1048576)
        self.assertEqual(audit['duplicate_payload_savings_bytes'],0)
        self.assertIn('angry',audit['native_emotions_using_neutral'])

    def test_codec_width_boundaries_and_full_dictionary_roundtrip(self):
        rng=random.Random(20261023)
        for bits in (2,3,4,6,8):
            for length in (1,2,3,7,100,4096,30000):
                data=bytes(rng.randrange(1<<bits) for _ in range(length))
                with self.subTest(bits=bits,length=length):
                    self.assertEqual(decode(encode(data,bits),bits,length),data)

    def test_truncated_gif_rejected(self):
        blob=unpack((ROOT/'whale/assets.bin').read_bytes())['w024.gif'][0]
        with self.assertRaises(ValueError):optimize(blob[:100])

    @unittest.skipUnless(importlib.util.find_spec('PIL'),'Optional independent GIF verifier needs Pillow')
    def test_all_real_gifs_are_exact_after_repacking_with_independent_decoder(self):
        from PIL import Image,ImageSequence
        def rendered(blob):
            image=Image.open(io.BytesIO(blob))
            return image.info.get('loop'),[(sha(f.convert('RGBA').tobytes()),f.info.get('duration',0)) for f in ImageSequence.Iterator(image)]
        files=unpack((ROOT/'whale/assets.bin').read_bytes())
        saved=0
        for name,(data,_,_) in files.items():
            if not name.endswith('.gif'):continue
            with self.subTest(file=name):
                packed=optimize(data)
                self.assertEqual(rendered(packed),rendered(data))
                self.assertLessEqual(len(packed),len(data))
                saved+=len(data)-len(packed)
        self.assertGreater(saved,0)


if __name__=='__main__':unittest.main()
