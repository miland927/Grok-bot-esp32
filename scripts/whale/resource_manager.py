"""Host-only resource workspace: import, inspect, stage and reproducibly package."""
import argparse
import io
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import uuid
import zipfile

from workflow_core import Lease, now, read_json, sha, write_json

ROOT=Path(__file__).resolve().parents[2]
TRIGGERS=('manual','neutral','happy','thinking','angry','embarrassed','sad','surprised','sleepy','idle','wake')


def unpack(blob):
    if len(blob)<12: raise ValueError('资源头不完整')
    count,checksum,length=struct.unpack_from('<III',blob)
    if not 1<=count<=512 or length+12!=len(blob) or sum(blob[12:])&0xffff!=checksum:
        raise ValueError('资源长度或校验不符')
    start=12+count*44
    if start>len(blob): raise ValueError('资源表不完整')
    files={}
    spans=[]
    for i in range(count):
        name,size,offset,w,h=struct.unpack_from('<32sIIHH',blob,12+i*44)
        name=name.rstrip(b'\0').decode('ascii')
        position=start+offset
        if not name or name in files or position+size+2>len(blob) or blob[position:position+2]!=b'ZZ':
            raise ValueError('资源记录不合法')
        spans.append((position,position+size+2))
        files[name]=(blob[position+2:position+size+2],w,h)
    spans.sort()
    if any(a[1]>b[0] for a,b in zip(spans,spans[1:])): raise ValueError('资源内容重叠')
    return files


def pack(files):
    table,payload=bytearray(),bytearray()
    for name,(data,w,h) in sorted(files.items()):
        encoded=name.encode('ascii')
        if not 0<len(encoded)<32: raise ValueError('资源文件名过长')
        table+=struct.pack('<32sIIHH',encoded,len(data),len(payload),w,h)
        payload+=b'ZZ'+data
    body=table+payload
    return struct.pack('<III',len(files),sum(body)&0xffff,len(body))+body


def gif_info(data):
    if len(data)<14 or data[:6] not in (b'GIF87a',b'GIF89a'): raise ValueError('不是有效GIF')
    w,h=struct.unpack_from('<HH',data,6)
    if not 1<=w<=512 or not 1<=h<=512: raise ValueError('GIF尺寸需在512像素以内')
    flags=data[10]
    position=13+(3*(2**((flags&7)+1)) if flags&128 else 0)
    frames=duration=0
    delay=0
    def blocks(position):
        while True:
            if position>=len(data): raise ValueError('GIF数据被截断')
            size=data[position]; position+=1
            if size==0: return position
            position+=size
            if position>len(data): raise ValueError('GIF子块被截断')
    while position<len(data):
        marker=data[position];position+=1
        if marker==0x3b:
            if not frames: raise ValueError('GIF没有图像帧')
            return dict(width=w,height=h,frames=frames,duration_ms=duration,inspection='structure_only')
        if marker==0x21:
            if position>=len(data): raise ValueError('GIF扩展被截断')
            label=data[position];position+=1
            if label==0xf9:
                if position+6>len(data) or data[position]!=4 or data[position+5]!=0: raise ValueError('GIF控制块错误')
                delay=struct.unpack_from('<H',data,position+2)[0]*10
                position+=6
            else: position=blocks(position)
        elif marker==0x2c:
            if position+9>len(data): raise ValueError('GIF图像头被截断')
            left,top,fw,fh=struct.unpack_from('<HHHH',data,position)
            local=data[position+8];position+=9
            if not fw or not fh or left+fw>w or top+fh>h: raise ValueError('GIF帧超出画布')
            if local&128: position+=3*(2**((local&7)+1))
            if position>=len(data) or not 2<=data[position]<=8: raise ValueError('GIF编码字段错误')
            position=blocks(position+1)
            frames+=1;duration+=delay;delay=0
            if frames>600: raise ValueError('单动作最多600帧')
        else: raise ValueError('GIF块类型错误')
    raise ValueError('GIF缺少结束标记')


class ResourceStore:
    def __init__(self,root=ROOT):
        self.root=Path(root).resolve()
        self.workspace=self.root/'local-only/manager'
        self.workspace.mkdir(parents=True,exist_ok=True)
        self.draft_path=self.workspace/'draft.json'

    def lease(self): return Lease(self.workspace/'locks','resources')
    def base_blob(self):
        reference=read_json(self.workspace/'device-reference.json')
        if not reference: return (self.root/'whale/assets.bin').read_bytes()
        path=(self.root/reference['resource_artifact']).resolve()
        if not path.is_relative_to(self.workspace/'device-sessions'): raise ValueError('设备基线路径非法')
        blob=path.read_bytes()
        if sha(blob)!=reference['resource_sha256']: raise ValueError('设备基线哈希变化，请重新核验')
        return blob

    def base(self): return unpack(self.base_blob())
    def draft(self): return read_json(self.draft_path,dict(revision=0,actions={}))

    def actions(self):
        library=read_json(self.root/'whale/library.json',dict(files=[]))
        labels={f['id'].lower():f['name'].removesuffix('.webm') for f in library['files']}
        result={}
        for filename,(data,w,h) in self.base().items():
            if filename.endswith('.gif'):
                identifier=filename.removesuffix('.gif')
                info=gif_info(data)
                result[identifier]=dict(id=identifier,label=labels.get(identifier,identifier),file=filename,
                                        sha256=sha(data),bytes=len(data),**info,trigger='manual',cooldown_ms=0,
                                        source='baseline',preview='/media/base/'+filename)
        for identifier,entry in self.draft()['actions'].items():
            result[identifier]={**result.get(identifier,{}),**entry,'source':'staged',
                                'preview':'/media/staged/'+entry['sha256']+'.gif'}
        return list(result.values())

    def import_data(self,data,filename,target=None):
        started=time.monotonic()
        original_sha=sha(data)
        if not data or len(data)>20*1048576: raise ValueError('单文件需在20MiB以内')
        suffix=Path(filename).suffix.lower()
        with self.lease():
            self.last_conversion_cache_hit=None
            if suffix=='.webm': data=self.convert_webm(data)
            elif suffix!='.gif': raise ValueError('支持GIF或VP9 WebM素材')
            info=gif_info(data)
            if (info['width'],info['height'])!=(160,160): raise ValueError('当前资源引擎画布为160×160；请先按该尺寸转换GIF，或导入WebM自动转换')
            if info['duration_ms']>15000: raise ValueError('此导入配置最多15秒，不会自动截短原动作')
            base_ids={a['id'] for a in self.actions()}
            if target and target not in base_ids: raise ValueError('找不到要替换的动作')
            identifier=target or ('u'+uuid.uuid4().hex[:10])
            previous=next((a for a in self.actions() if a['id']==target),None) if target else None
            digest=sha(data)
            media=self.workspace/'media';media.mkdir(exist_ok=True)
            (media/(digest+'.gif')).write_bytes(data)
            entry=dict(id=identifier,label=Path(filename).stem[:24],file=(identifier+'.gif'),
                       sha256=digest,bytes=len(data),**info,trigger='manual',cooldown_ms=0)
            if previous:
                entry.update(label=previous['label'],trigger=previous['trigger'],cooldown_ms=previous['cooldown_ms'],
                             config_changed=previous.get('config_changed',False))
            draft=self.draft();draft['actions'][identifier]=entry;draft['revision']+=1
            entry.update(processing_seconds=round(time.monotonic()-started,3),conversion_cache_hit=self.last_conversion_cache_hit)
            write_json(self.draft_path,draft)
            write_json(self.workspace/'imports'/(uuid.uuid4().hex+'.json'),dict(created_at=now(),action_id=identifier,
                       source_sha256=original_sha,processing_seconds=entry['processing_seconds'],conversion_cache_hit=self.last_conversion_cache_hit,
                       firmware_compiled=False,firmware_written=False,model_token_usage=None))
            return entry

    def convert_webm(self,data):
        ffmpeg,ffprobe=shutil.which('ffmpeg'),shutil.which('ffprobe')
        if not ffmpeg or not ffprobe: raise ValueError('未找到现有FFmpeg/FFprobe，未自动安装')
        originals=self.workspace/'originals';originals.mkdir(exist_ok=True)
        source=originals/(sha(data)+'.webm')
        if not source.exists(): source.write_bytes(data)
        probe=subprocess.run([ffprobe,'-v','error','-show_entries','format=duration:stream=codec_name','-of','json',str(source)],capture_output=True,timeout=20)
        if probe.returncode: raise ValueError('WebM无法读取')
        metadata=json.loads(probe.stdout)
        if float(metadata['format']['duration'])>15: raise ValueError('素材超过15秒，需要先人工选择完整动作范围')
        if not any(s.get('codec_name')=='vp9' for s in metadata['streams']): raise ValueError('当前自动转换仅支持VP9 WebM')
        version=subprocess.run([ffmpeg,'-version'],capture_output=True,timeout=10).stdout.splitlines()[0].decode(errors='replace')
        key=sha(data+('160/8/64/none/'+version).encode())
        cache=self.workspace/'converted';cache.mkdir(exist_ok=True)
        target=cache/(key+'.gif')
        self.last_conversion_cache_hit=target.exists()
        if not target.exists():
            temporary=cache/(key+'.partial.gif')
            graph='fps=8,scale=160:160:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:reserve_transparent=1[p];[b][p]paletteuse=dither=none'
            command=[ffmpeg,'-v','error','-y','-c:v','libvpx-vp9','-i',str(source),'-filter_complex',graph,'-an','-loop','0',str(temporary)]
            try:
                result=subprocess.run(command,capture_output=True,timeout=90)
                (cache/(key+'.log')).write_bytes(result.stdout+result.stderr)
                if result.returncode: raise ValueError('转换失败，完整日志已留在本机转换目录')
                gif_info(temporary.read_bytes())
                temporary.replace(target)
            finally:
                if temporary.exists(): temporary.unlink()
        return target.read_bytes()

    def edit(self,identifier,values):
        if not re.fullmatch(r'[a-z0-9_-]{1,24}',identifier): raise ValueError('无效动作编号')
        with self.lease():
            current=next((a for a in self.actions() if a['id']==identifier),None)
            if current is None: raise ValueError('找不到动作')
            label=str(values.get('label',current['label'])).strip()
            trigger=values.get('trigger',current['trigger'])
            cooldown=values.get('cooldown_ms',current['cooldown_ms'])
            if not label or len(label)>24 or trigger not in TRIGGERS or type(cooldown)!=int or not 0<=cooldown<=3600000:
                raise ValueError('名称、触发类型或冷却时间不合法')
            media=self.workspace/'media';media.mkdir(exist_ok=True)
            if current['source']=='baseline':
                data=self.base()[current['file']][0]
                (media/(current['sha256']+'.gif')).write_bytes(data)
            current.update(label=label,trigger=trigger,cooldown_ms=cooldown,config_changed=True)
            current.pop('preview',None);current.pop('source',None)
            draft=self.draft();draft['actions'][identifier]=current;draft['revision']+=1
            write_json(self.draft_path,draft)
            return current

    def clear_draft(self):
        with self.lease(): write_json(self.draft_path,dict(revision=self.draft()['revision']+1,actions={}))

    def package(self):
        with self.lease():
            base_blob=self.base_blob()
            base=unpack(base_blob);files=dict(base)
            actions=self.actions();draft=self.draft()['actions']
            if not draft:
                report=dict(created_at=now(),status='no_changes',sha256=sha(base_blob),base_sha256=sha(base_blob),
                            draft_revision=self.draft()['revision'],
                            bytes=len(base_blob),bytes_to_transfer=0,changed_media=[],new_action_ids=[],
                            action_count=len(actions),engine_upgrade_required_for_registry=False,
                            firmware_compiled=False,firmware_written=False,activation='no_changes')
                write_json(self.workspace/'latest-package.json',report)
                return report
            base_ids={name.removesuffix('.gif') for name in base if name.endswith('.gif')}
            changed=[]
            for identifier,entry in draft.items():
                data=(self.workspace/'media'/(entry['sha256']+'.gif')).read_bytes()
                if sha(data)!=entry['sha256']: raise ValueError('暂存素材哈希发生变化')
                info=gif_info(data)
                files[entry['file']]=(data,info['width'],info['height'])
                if entry['file'] not in base or base[entry['file']][0]!=data: changed.append(entry['file'])
            index=json.loads(base['index.json'][0].rstrip(b'\0'))
            names={x['name'] for x in index['emoji_collection']}
            for action in actions:
                if action['id'] not in names:
                    index['emoji_collection'].append(dict(name=action['id'],file=action['file']))
            definition=dict(schema_version=1,engine_abi=1,actions=[{k:a[k] for k in ('id','label','file','sha256','trigger','cooldown_ms')} for a in sorted(actions,key=lambda a:a['id'])])
            files['whale-actions.json']=(json.dumps(definition,ensure_ascii=False,separators=(',',':')).encode(),0,0)
            index['whale_actions']=dict(file='whale-actions.json',schema_version=1)
            files['index.json']=(json.dumps(index,separators=(',',':')).encode(),0,0)
            output=pack(files)
            profile=read_json(self.root/'whale/hardware-profile.json')
            capacity=int(profile['allowed_write_partitions']['assets']['size'],0)
            if len(output)>int(capacity*.9): raise ValueError('候选包无法保留10%空间；未更新任何设备')
            assert unpack(output)==files
            for name,value in base.items():
                if not name.endswith('.gif') and name!='index.json' and name!='whale-actions.json':
                    if files[name]!=value: raise ValueError('固定字体或模型发生变化')
            new_ids=[a['id'] for a in actions if a['id'] not in base_ids]
            needs_engine=bool(new_ids or any(e.get('config_changed') for e in draft.values()))
            directory=self.workspace/'packages'/sha(output)
            directory.mkdir(parents=True,exist_ok=True)
            (directory/'assets.bin').write_bytes(output)
            report=dict(created_at=now(),sha256=sha(output),base_sha256=sha(base_blob),bytes=len(output),
                        draft_revision=self.draft()['revision'],
                        capacity=capacity,free_percent=round(100*(capacity-len(output))/capacity,2),
                        changed_media=changed,new_action_ids=new_ids,action_count=len(actions),
                        transfer_mode='whole_resource_bundle',bytes_to_transfer=len(output),
                        engine_upgrade_required_for_registry=needs_engine,
                        activation='blocked_until_dynamic_engine_verified' if needs_engine else 'not_installed',
                        firmware_compiled=False,firmware_written=False,physical_device_checked=False,
                        unchanged_fixed_resources_verified=True,artifact='local-only/manager/packages/'+sha(output)+'/assets.bin')
            write_json(directory/'manifest.json',report)
            # Delta is a host artifact only; no claim that current firmware can install it.
            with zipfile.ZipFile(directory/'changed-objects.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
                for name in changed+['whale-actions.json','index.json']:
                    archive.writestr(name,files[name][0])
                archive.writestr('PATCH-NOT-INSTALLABLE.json',json.dumps(dict(base_sha256=sha(base_blob),target_sha256=sha(output),device_installer_available=False)))
            write_json(self.workspace/'latest-package.json',report)
            return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=['package','list'])
    args=parser.parse_args()
    try:
        store=ResourceStore()
        print(json.dumps(store.package() if args.operation=='package' else store.actions(),ensure_ascii=False,indent=2))
    except (OSError,ValueError,KeyError) as error:
        print('ERROR: '+str(error))
        raise SystemExit(2)
