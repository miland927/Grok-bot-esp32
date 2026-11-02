"""USB installer tests use simulated Flash only; never open a serial port."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/whale'))
from device_resources import DeviceResources, OFFSET, CAPACITY, FLASH, parse_partitions
from resource_manager import ResourceStore
from workflow_core import read_json, sha, write_json

MAC='aa:bb:cc:dd:ee:ff'


def table():
    entries=[('nvs',1,2,0x9000,0x6000),('otadata',1,0,0xf000,0x2000),
             ('phy_init',1,1,0x11000,0x1000),('model',1,0x82,0x12000,0xee000),
             ('factory',0,0,0x100000,0x900000),('ota_0',0,0x10,0xa00000,0x600000),
             ('ota_1',0,0x11,0x1000000,0x600000),('assets',1,0x82,OFFSET,CAPACITY)]
    raw=b''.join(struct.pack('<HBBII16sI',0x50aa,t,st,o,s,n.encode(),0) for n,t,st,o,s in entries)
    raw+=b'\xeb\xeb'+b'\xff'*14+hashlib.md5(raw).digest()
    return raw.ljust(4096,b'\xff')


class DeviceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        for name in ('assets.bin','library.json','hardware-profile.json'):
            target=self.root/'whale'/name;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(ROOT/'whale'/name,target)
        self.store=ResourceStore(self.root)
        self.blob=self.store.base_blob()
        self.memory=bytearray(b'\xff'*FLASH)
        self.memory[0x8000:0x9000]=table()
        self.memory[OFFSET:OFFSET+len(self.blob)]=self.blob
        self.protected_hash=sha(self.memory[:OFFSET])
        self.writes=[];self.reboots=0;self.fail=False
        owner=self
        class Transport:
            def __init__(self,settings,directory):self.settings=settings
            def probe(self):
                if self.settings['identity']!=MAC:raise ValueError('identity mismatch')
                parse_partitions(bytes(owner.memory[0x8000:0x9000]))
            def read(self,offset,size,target):Path(target).write_bytes(owner.memory[offset:offset+size])
            def verify(self,offset,path):
                blob=Path(path).read_bytes()
                if bytes(owner.memory[offset:offset+len(blob)])!=blob:raise ValueError('verify mismatch')
            def write(self,path):
                data=Path(path).read_bytes();owner.writes.append((OFFSET,len(data)))
                owner.memory[OFFSET:OFFSET+len(data)]=data
                if owner.fail: owner.fail=False;raise ValueError('simulated interrupted verification')
            def reboot(self):owner.reboots+=1
        self.device=DeviceResources(self.root,Transport)
        write_json(self.store.workspace/'device.json',dict(port='COM3',identity=MAC))

    def tearDown(self): self.temp.cleanup()
    def sync(self):return self.device.perform('sync')
    def candidate(self):
        self.store.import_data(self.store.base()['w031.gif'][0],'replacement.gif','w024')
        return self.store.package()

    def test_sync_install_rollback_preserves_application_and_nvs(self):
        self.sync();candidate=self.candidate()
        result=self.device.perform('install',candidate['sha256'])
        self.assertEqual(result['status'],'installed_verified')
        self.assertFalse(result['physical_playback_confirmed'])
        self.assertEqual(self.store.base()['w024.gif'],self.store.base()['w031.gif'])
        self.assertEqual(sha(self.memory[:OFFSET]),self.protected_hash)
        self.assertTrue(all(offset==OFFSET and size<=CAPACITY for offset,size in self.writes))
        self.device.perform('rollback')
        self.assertEqual(bytes(self.memory[OFFSET:OFFSET+len(self.blob)]),self.blob)
        self.assertEqual(self.store.base_blob(),self.blob)
        self.assertEqual(len(self.writes),2)
        with self.assertRaises(ValueError):self.device.perform('rollback')

    def test_wrong_identity_never_writes(self):
        write_json(self.store.workspace/'device.json',dict(port='COM3',identity='11:22:33:44:55:66'))
        with self.assertRaises(ValueError):self.sync()
        self.assertFalse(self.writes)

    def test_stale_candidate_never_writes(self):
        self.sync();candidate=self.candidate()
        self.store.import_data(self.store.base()['w035.gif'][0],'new.gif','w024')
        with self.assertRaises(ValueError):self.device.perform('install',candidate['sha256'])
        self.assertFalse(self.writes)

    def test_new_action_is_not_installable(self):
        self.sync()
        self.store.import_data(self.store.base()['w031.gif'][0],'new.gif')
        candidate=self.store.package()
        with self.assertRaises(ValueError):self.device.perform('install',candidate['sha256'])
        self.assertFalse(self.writes)

    def test_changed_application_blocks_install(self):
        self.sync();candidate=self.candidate();self.memory[0x1000040]^=1
        with self.assertRaises(ValueError):self.device.perform('install',candidate['sha256'])
        self.assertFalse(self.writes)

    def test_changed_device_resource_blocks_install(self):
        self.sync();candidate=self.candidate();self.memory[OFFSET+5000]^=1
        with self.assertRaises(ValueError):self.device.perform('install',candidate['sha256'])
        self.assertFalse(self.writes)

    def test_corrupt_backup_blocks_install(self):
        self.sync();candidate=self.candidate()
        session=read_json(self.store.workspace/'device-backup.json')['session']
        (self.store.workspace/'device-sessions'/session/'full-flash-32MB.bin').write_bytes(b'bad')
        with self.assertRaises(ValueError):self.device.perform('install',candidate['sha256'])
        self.assertFalse(self.writes)

    def test_failed_write_can_restore_backup(self):
        self.sync();candidate=self.candidate();self.fail=True
        with self.assertRaises(ValueError):self.device.perform('install',candidate['sha256'])
        last=read_json(self.store.workspace/'last-device-write.json')
        self.assertEqual(last['status'],'write_failed')
        self.device.perform('rollback')
        self.assertEqual(self.store.base_blob(),self.blob)
        self.assertEqual(sha(self.memory[:OFFSET]),self.protected_hash)

    def test_sync_refuses_to_discard_drafts(self):
        self.candidate()
        with self.assertRaises(ValueError):self.sync()
        self.assertTrue(self.store.draft()['actions'])

    def test_normal_nvs_changes_before_update_are_preserved(self):
        self.sync();candidate=self.candidate();self.memory[0x9100]=17
        self.device.perform('install',candidate['sha256'])
        self.assertEqual(self.memory[0x9100],17)
        self.device.perform('rollback')
        self.assertEqual(self.memory[0x9100],17)

    def test_interrupted_write_intent_can_be_restored(self):
        self.sync();candidate=self.candidate();self.fail=True
        with self.assertRaises(ValueError):self.device.perform('install',candidate['sha256'])
        path=self.store.workspace/'last-device-write.json'
        last=read_json(path);last['status']='writing';write_json(path,last)
        self.device.perform('rollback')
        self.assertEqual(self.store.base_blob(),self.blob)

    def test_partition_md5_or_layout_mismatch_is_rejected(self):
        raw=bytearray(table());raw[10]^=1
        with self.assertRaises(ValueError):parse_partitions(bytes(raw))


if __name__=='__main__':unittest.main()
