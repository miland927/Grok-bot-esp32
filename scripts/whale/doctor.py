"""Offline repository inspection; no package install, serial access or Flash writes."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def inspect(root=ROOT):
    root=Path(root).resolve()
    errors, warnings=[], []
    profile=json.loads((root/'whale/hardware-profile.json').read_text(encoding='utf-8'))
    board=(root/profile['board_directory']).resolve()
    if not board.is_relative_to(root): raise ValueError('板型路径超出仓库')
    config=(board/'config.h').read_text(encoding='utf-8')
    variants=json.loads((board/'config.json').read_text(encoding='utf-8'))
    if variants['target']!=profile['chip']: errors.append('板型芯片与档案不一致')
    if profile['build_variant'] not in [v['name'] for v in variants['builds']]: errors.append('缺少指定构建变体')
    for define,key in [('DISPLAY_WIDTH','width'),('DISPLAY_HEIGHT','height')]:
        found=re.search(r'^#define\s+'+define+r'\s+(\d+)',config,re.M)
        if not found or int(found[1])!=profile['display'][key]: errors.append(define+'不匹配')
    partitions={}
    def number(value):
        value=value.strip().lower()
        if value.endswith(('k','m')): return int(value[:-1],0)*(1024 if value[-1]=='k' else 1048576)
        return int(value,0)
    for line in (root/profile['partition_table']).read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'): continue
        cells=[x.strip() for x in line.split(',')]
        partitions[cells[0]]=(number(cells[3]),number(cells[4]))
    ranges=sorted((start,start+size,key) for key,(start,size) in partitions.items())
    for first,last in zip(ranges,ranges[1:]):
        if first[1]>last[0]: errors.append('分区重叠：'+first[2]+'/'+last[2])
    if not ranges or ranges[-1][1]>profile['flash_bytes']: errors.append('分区超出Flash')
    for name,item in profile['allowed_write_partitions'].items():
        if partitions.get(name)!=(number(item['offset']),number(item['size'])): errors.append('允许写入分区不匹配：'+name)
    for name in profile['protected_partitions']:
        if name not in partitions: errors.append('缺少保护分区：'+name)
    machine_file=root/'local-only/machine.json'
    machine=json.loads(machine_file.read_text(encoding='utf-8-sig')) if machine_file.exists() else {}
    sdk=machine.get('esp_idf_path') or os.environ.get('IDF_PATH')
    version=None
    if sdk:
        version_path=Path(sdk)/'tools/cmake/version.cmake'
        if version_path.is_file():
            metadata=version_path.read_text(encoding='utf-8')
            major=re.search(r'set\(IDF_VERSION_MAJOR\s+(\d+)',metadata)
            minor=re.search(r'set\(IDF_VERSION_MINOR\s+(\d+)',metadata)
            if major and minor: version=major[1]+'.'+minor[1]
    if version!=profile['esp_idf_version']: warnings.append('ESP-IDF需'+profile['esp_idf_version']+'；本机SDK元数据：'+str(version))
    if not (root/'dependencies.lock').exists(): errors.append('缺少依赖锁')
    if not (root/'PROJECT_STATE.md').exists(): errors.append('缺少唯一当前状态')
    if not machine_file.exists(): warnings.append('尚未填写本机配置；不会猜测串口和设备身份')
    git={}
    for key,args in [('commit',['rev-parse','--short','HEAD']),('branch',['branch','--show-current']),('dirty',['status','--porcelain'])]:
        result=subprocess.run(['git','-c','safe.directory='+str(root),'-C',str(root),*args],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=10)
        git[key]=result.stdout.strip() if result.returncode==0 else None
    if git['dirty']: warnings.append('有未提交改动；未自动合并、拉取或切换版本')
    wake=root/'whale/wake-words.json'
    if wake.exists():
        data=json.loads(wake.read_text(encoding='utf-8'))
        text=(root/'main/audio/wake_words/custom_wake_word.cc').read_text(encoding='utf-8')
        override=re.search(r'set_det_threshold\(multinet_model_data_,\s*([0-9.]+)f?\)',text)
        if override and float(override[1])!=data['recognizer']['threshold']: warnings.append('语音运行门限与JSON不一致')
    else: warnings.append('当前仓库未包含交接包所述v7语音配置')
    return dict(status='error' if errors else ('needs_attention' if warnings else 'checked'),
                board=profile['manufacturer']+' '+profile['product'],profile=profile,
                sdk_metadata_version=version,git=git,errors=errors,warnings=warnings,
                physical_device_checked=False,firmware_written=False,
                device_status='not_checked',metrics={'cpu':None,'psram_free':None,'fps':None})


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json',action='store_true')
    args=parser.parse_args()
    try: result=inspect()
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        result=dict(status='error',errors=[str(error)[:400]],warnings=[])
    print(json.dumps(result,ensure_ascii=False,indent=2) if args.json else '\n'.join([result['status'],*result['errors'],*result['warnings']]))
    raise SystemExit(2 if result['errors'] else 0)
