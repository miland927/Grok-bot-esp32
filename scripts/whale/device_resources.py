"""Guarded USB resource-only updates. No application, bootloader or NVS writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import uuid

from resource_manager import ResourceStore, unpack
from workflow_core import ExperimentLedger, device_lease, now, read_json, sha, write_json

ROOT=Path(__file__).resolve().parents[2]
OFFSET=0x1600000
CAPACITY=0xa00000
FLASH=0x2000000


def parse_partitions(data):
    result={}
    entries=bytearray()
    md5_seen=False
    for pos in range(0,len(data)-31,32):
        item=data[pos:pos+32]
        if item[:2]==b'\xeb\xeb':
            if item[16:]!=hashlib.md5(entries).digest(): raise ValueError('分区表MD5不符')
            md5_seen=True
            break
        if item[:2]!=b'\xaaP': break
        _,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',item)
        name=label.split(b'\0')[0].decode('ascii')
        if not name or name in result or offset+size>FLASH or flags: raise ValueError('分区表不合法或已加密')
        if any(offset<r['offset']+r['size'] and r['offset']<offset+size for r in result.values()): raise ValueError('分区表重叠')
        result[name]=dict(type=kind,subtype=subtype,offset=offset,size=size)
        entries+=item
    if not md5_seen: raise ValueError('需要有MD5保护的分区表')
    expected={'nvs':(1,2,0x9000,0x6000),'otadata':(1,0,0xf000,0x2000),
              'phy_init':(1,1,0x11000,0x1000),'model':(1,0x82,0x12000,0xee000),
              'factory':(0,0,0x100000,0x900000),'ota_0':(0,0x10,0xa00000,0x600000),
              'ota_1':(0,0x11,0x1000000,0x600000),'assets':(1,0x82,OFFSET,CAPACITY)}
    if set(result)!=set(expected) or any(tuple(result[k][f] for f in ('type','subtype','offset','size'))!=v for k,v in expected.items()):
        raise ValueError('设备分区不匹配该板32MB布局；禁止写入')
    return result


def resource_blob(partition):
    if len(partition)<12: raise ValueError('设备资源头不完整')
    length=struct.unpack_from('<I',partition,8)[0]+12
    if not 12<length<=CAPACITY or length>len(partition): raise ValueError('设备资源长度非法')
    blob=partition[:length]
    unpack(blob)
    return blob


def config(root):
    value=read_json(Path(root)/'local-only/manager/device.json',{})
    port=value.get('port','')
    identity=value.get('identity','').lower()
    if not re.fullmatch(r'COM[1-9][0-9]*|/dev/(?:tty|cu\.)[A-Za-z0-9._/-]+',port): raise ValueError('请先填写串口，例如COM3')
    if not re.fullmatch(r'(?:[0-9a-f]{2}:){5}[0-9a-f]{2}',identity): raise ValueError('请填写目标设备MAC以核验身份')
    return {**value,'identity':identity}


class EspTransport:
    def __init__(self,settings,directory):
        self.settings=settings
        self.directory=Path(directory)
        self.count=0
        self.first=True

    def run(self,args,before=None,after='no-reset',timeout=300):
        before=before or ('default-reset' if self.first else 'no-reset')
        self.first=False
        env=os.environ.copy()
        tools=self.settings.get('tools_directory','')
        if tools:
            if not Path(tools,'esptool').is_dir(): raise ValueError('工具目录缺少esptool包')
            env['PYTHONPATH']=tools+os.pathsep+env.get('PYTHONPATH','')
        command=[sys.executable,'-m','esptool','--chip','esp32s3','--port',self.settings['port'],
                 '--baud','460800','--before',before,'--after',after,*args]
        self.count+=1
        print('阶段 '+str(self.count)+': '+args[0],flush=True)
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=timeout)
        (self.directory/(f'{self.count:02d}-'+args[0]+'.log')).write_bytes(result.stdout)
        if result.returncode: raise ValueError('串口阶段失败：'+args[0]+'；完整错误见本机日志')
        return result.stdout.decode('utf-8',errors='replace')

    def probe(self):
        identity=self.run(['flash-id'])
        if self.settings['identity'] not in identity.lower() or 'Detected flash size: 32MB' not in identity or 'ESP32-S3' not in identity:
            raise ValueError('芯片身份或32MB容量不匹配')
        security=self.run(['get-security-info'])
        if 'Secure Boot: Disabled' not in security or 'Flash Encryption: Disabled' not in security:
            raise ValueError('安全启动/加密状态不支持此更新方式')
        table=self.directory/'partition-table.bin'
        self.run(['read-flash','0x8000','0x1000',str(table)])
        parse_partitions(table.read_bytes())

    def read(self,offset,size,target):
        self.run(['read-flash',hex(offset),hex(size),str(target)],timeout=600)

    def verify(self,offset,path):
        self.run(['verify-flash',hex(offset),str(path)],timeout=180)

    def write(self,path):
        # The only writable address in this module. esptool is never passed --force.
        if not 12<Path(path).stat().st_size<=CAPACITY: raise ValueError('资源写入范围非法')
        self.run(['write-flash',hex(OFFSET),str(path)],timeout=600)

    def reboot(self):
        self.run(['read-mac'],after='hard-reset',timeout=20)


class DeviceResources:
    def __init__(self,root=ROOT,transport=EspTransport):
        self.root=Path(root).resolve()
        self.store=ResourceStore(root)
        self.workspace=self.store.workspace
        self.transport=transport

    def reference(self): return read_json(self.workspace/'device-reference.json')

    def safe_path(self,name):
        path=(self.root/name).resolve()
        if not path.is_relative_to(self.workspace/'device-sessions'): raise ValueError('设备备份路径不合法')
        return path

    def save_reference(self,record,blob_path):
        write_json(self.workspace/'device-reference.json',dict(identity=record['identity'],
                   resource_sha256=sha(blob_path.read_bytes()),resource_artifact=blob_path.relative_to(self.root).as_posix(),
                   session=record['session'],updated_at=now(),source='verified_usb_readback' if record['operation']=='sync' else 'usb_write_verified_by_flash_md5',
                   physical_playback_confirmed=False))
        write_json(self.store.draft_path,dict(revision=self.store.draft()['revision']+1,actions={}))

    def perform(self,operation,expected_sha=None):
        settings=config(self.root)
        identifier=uuid.uuid4().hex
        directory=self.workspace/'device-sessions'/identifier
        directory.mkdir(parents=True)
        record=dict(operation=operation,session=identifier,identity=settings['identity'],started_at=now(),
                    status='checking',application_written=False,assets_written=False,physical_playback_confirmed=False)
        record_path=directory/'result.json'
        write_json(record_path,record)
        transport=self.transport(settings,directory)
        with device_lease(settings['identity']),self.store.lease():
            try:
                transport.probe()
                record['identity_partition_verified']=True
                if operation=='sync':
                    if self.store.draft()['actions']: raise ValueError('请先撤销或保存草稿；同步设备基线不会覆盖草稿')
                    snapshot=directory/'full-flash-32MB.bin'
                    transport.read(0,FLASH,snapshot)
                    full=snapshot.read_bytes()
                    if len(full)!=FLASH: raise ValueError('完整设备备份长度不符')
                    transport.verify(0,snapshot)
                    parse_partitions(full[0x8000:0x9000])
                    blob=resource_blob(full[OFFSET:])
                    # Validate all GIFs before changing the active host baseline.
                    from resource_manager import gif_info
                    for name,(data,_,_) in unpack(blob).items():
                        if name.endswith('.gif'): gif_info(data)
                    assets=directory/'assets.bin';assets.write_bytes(blob)
                    protected=directory/'protected-before.bin';protected.write_bytes(full[:OFFSET])
                    rollback=directory/'assets-full-partition.bin';rollback.write_bytes(full[OFFSET:])
                    record.update(status='backup_verified',full_backup_sha256=sha(full),
                                  resource_sha256=sha(blob),protected_sha256=sha(full[:OFFSET]),
                                  backup_verified=True,resource_bytes=len(blob))
                    self.save_reference(record,assets)
                    write_json(self.workspace/'device-backup.json',record)
                elif operation in ('install','rollback'):
                    reference=self.reference()
                    backup=read_json(self.workspace/'device-backup.json')
                    if not reference or not backup or reference['identity']!=settings['identity'] or backup['identity']!=settings['identity']:
                        raise ValueError('需要先核验并备份这台设备')
                    prior=self.workspace/'device-sessions'/backup['session']
                    protected=prior/'protected-before.bin'
                    rollback=prior/'assets-full-partition.bin'
                    full=prior/'full-flash-32MB.bin'
                    if sha(full.read_bytes())!=backup['full_backup_sha256'] or sha(protected.read_bytes())!=backup['protected_sha256'] or rollback.read_bytes()!=full.read_bytes()[OFFSET:]:
                        raise ValueError('本机备份已变化；禁止更新')
                    # NVS/OTA metadata may legitimately change after a normal boot.
                    # Compare immutable code to the backup, then capture current
                    # metadata and prove it stayed byte-identical during this write.
                    tail=directory/'protected-code.bin'
                    tail.write_bytes(full.read_bytes()[0x12000:OFFSET])
                    transport.verify(0x12000,tail)
                    metadata=directory/'metadata-before.bin'
                    transport.read(0,0x12000,metadata)
                    if metadata.read_bytes()[:0x9000]!=full.read_bytes()[:0x9000]:
                        raise ValueError('启动程序或分区表与备份不同，请重新核验')
                    if operation=='install':
                        candidate=read_json(self.workspace/'latest-package.json')
                        if not candidate or candidate.get('sha256')!=expected_sha or candidate.get('draft_revision')!=self.store.draft()['revision']:
                            raise ValueError('候选已变化，请重新生成并核对')
                        if candidate.get('engine_upgrade_required_for_registry') or not candidate.get('changed_media'):
                            raise ValueError('当前只支持替换已有动作；新增菜单、规则或空更新不可安装')
                        if candidate['base_sha256']!=reference['resource_sha256']: raise ValueError('候选基线与设备基线不匹配')
                        target=(self.root/candidate['artifact']).resolve()
                        if not target.is_relative_to(self.workspace/'packages') or sha(target.read_bytes())!=expected_sha: raise ValueError('候选文件不匹配')
                        original=self.safe_path(reference['resource_artifact'])
                        if sha(original.read_bytes())!=reference['resource_sha256']: raise ValueError('基线文件哈希不符')
                        transport.verify(OFFSET,original)
                        before=unpack(original.read_bytes());after=unpack(target.read_bytes())
                        if set(after)-set(before)-{'whale-actions.json'}: raise ValueError('候选含未知新增资源')
                        for name,value in before.items():
                            if not name.endswith('.gif') and name not in ('index.json','whale-actions.json') and after.get(name)!=value:
                                raise ValueError('候选更改字体或模型')
                        old_index=json.loads(before['index.json'][0].rstrip(b'\0'))
                        new_index=json.loads(after['index.json'][0].rstrip(b'\0'))
                        old_index.pop('whale_actions',None);new_index.pop('whale_actions',None)
                        if old_index!=new_index: raise ValueError('仅媒体更新不得更改资源索引或菜单')
                        from resource_manager import gif_info
                        for name,value in after.items():
                            if name.endswith('.gif') and value!=before.get(name):
                                info=gif_info(value[0])
                                if (info['width'],info['height'])!=(160,160) or info['duration_ms']>15000:
                                    raise ValueError('替换媒体尺寸或时长不符')
                        target_blob=target.read_bytes()
                        unpack(target_blob)
                        if len(target_blob)>int(CAPACITY*.9): raise ValueError('资源余量不足10%')
                        experiment='resource-'+sha((settings['identity']+expected_sha).encode())
                        record['candidate_sha256']=expected_sha
                    else:
                        last=read_json(self.workspace/'last-device-write.json')
                        if not last or last['identity']!=settings['identity'] or last.get('backup_session')!=backup['session'] or last['status'] not in ('installed_verified','write_failed','writing'):
                            raise ValueError('没有可回退的当前资源更新')
                        if last['status']=='installed_verified':
                            transport.verify(OFFSET,self.safe_path(reference['resource_artifact']))
                        target=rollback
                        target_blob=resource_blob(rollback.read_bytes())
                        experiment=last['experiment']
                    ExperimentLedger(self.workspace).reserve_write(experiment)
                    record.update(backup_session=backup['session'],experiment=experiment,status='writing',assets_written=True)
                    write_json(record_path,record)
                    write_json(self.workspace/'last-device-write.json',record)
                    record['assets_written']=True  # May be partial if transport fails.
                    try:
                        transport.write(target)
                        transport.verify(OFFSET,target)
                        transport.verify(0,metadata)
                        transport.verify(0x12000,tail)
                    except Exception:
                        record['status']='write_failed'
                        write_json(self.workspace/'last-device-write.json',record)
                        raise
                    verified=directory/'assets.bin';verified.write_bytes(target_blob)
                    self.save_reference(record,verified)
                    record.update(status='installed_verified' if operation=='install' else 'rolled_back_verified',
                                  resource_sha256=sha(target_blob),protected_unchanged=True)
                    write_json(self.workspace/'last-device-write.json',record)
                else: raise ValueError('未知设备操作')
            except Exception as error:
                record['error']=str(error)[:500]
                if record['status']!='write_failed': record['status']='failed'
                raise
            finally:
                try:
                    transport.reboot()
                    record['reboot_requested']=True
                except Exception as error: record['reboot_error']=str(error)[:300]
                record['ended_at']=now()
                write_json(record_path,record)
                write_json(self.workspace/'latest-device-operation.json',record)
        print(json.dumps(record,ensure_ascii=False),flush=True)
        if record.get('reboot_error'): raise ValueError('资源阶段结束，但设备重启失败；请检查本机日志后手动重启')
        return record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=['sync','install','rollback'])
    parser.add_argument('--sha256')
    args=parser.parse_args()
    try: DeviceResources().perform(args.operation,args.sha256)
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),flush=True)
        return 1
    return 0


if __name__=='__main__': raise SystemExit(main())
