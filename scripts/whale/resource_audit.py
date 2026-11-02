"""Offline Flash accounting; never treats resource occupancy as RAM/CPU usage."""
import json
import struct
import re

from resource_manager import gif_info,unpack
from workflow_core import sha


def inspect_resources(blob,capacity,flash_capacity=32*1048576):
    files=unpack(blob)
    index=json.loads(files['index.json'][0].rstrip(b'\0'))
    font=index.get('text_font');speech=index.get('srmodels')
    categories={k:dict(bytes=0,count=0) for k in ('animations','fonts','speech_models','other_images','metadata','other')}
    entries=[];groups={};referenced=set()
    def strings(value):
        if isinstance(value,str):
            if value in files:referenced.add(value)
        elif isinstance(value,dict):
            for child in value.values():strings(child)
        elif isinstance(value,list):
            for child in value:strings(child)
    strings(index);referenced.add('index.json')
    for name,(data,width,height) in sorted(files.items()):
        category='animations' if name.endswith('.gif') else 'fonts' if name==font else 'speech_models' if name==speech else 'other_images' if name.endswith(('.png','.jpg','.jpeg')) else 'metadata' if name.endswith('.json') else 'other'
        categories[category]['bytes']+=len(data);categories[category]['count']+=1
        entry=dict(file=name,bytes=len(data),category=category,referenced_in_index=name in referenced)
        if category=='animations':
            info=gif_info(data)
            entry.update(width=info['width'],height=info['height'],frames=info['frames'],duration_ms=info['duration_ms'],
                         encoded_fps=round(info['frames']*1000/info['duration_ms'],3) if info['duration_ms'] else None)
        entries.append(entry);groups.setdefault(sha(data),[]).append(name)
    overhead=len(blob)-sum(item['bytes'] for item in categories.values())
    if overhead!=12+46*len(files):raise ValueError('Accounting does not reconcile with resource layout')
    aliases=index.get('emoji_collection',[])
    # Aliases only reference a file; they are not duplicate file payloads.
    neutral=next((a.get('file') for a in aliases if a.get('name')=='neutral'),None)
    return dict(scope='resource_partition_only',bytes=len(blob),capacity=capacity,
                used_percent=round(100*len(blob)/capacity,2),physical_flash_capacity=flash_capacity,
                resource_share_of_physical_flash_percent=round(100*len(blob)/flash_capacity,2),
                free_bytes=capacity-len(blob),packing_headroom_bytes=int(capacity*.9)-len(blob),
                categories=categories,container_overhead_bytes=overhead,files=entries,
                duplicate_payload_savings_bytes=sum((len(names)-1)*len(files[names[0]][0]) for names in groups.values()),
                not_referenced_in_index=[n for n in files if n not in referenced],
                emotion_aliases=aliases,
                native_emotions_using_neutral=[a['name'] for a in aliases if a.get('file')==neutral and a.get('name')!='neutral' and not re.fullmatch(r'w[0-9]{3}',a.get('name',''))],
                runtime_cpu_percent=None,runtime_ram_bytes=None,actual_render_fps=None)
