"""Palette/LZW repacking; preserves frame blocks, timing and palette colors."""
import struct,math


def decode(data,minimum,limit):
    clear=1<<minimum;end=clear+1;width=minimum+1;next_code=end+1
    dictionary={i:bytes([i]) for i in range(clear)}
    position=0;previous=None;output=bytearray()
    while position+width<=len(data)*8:
        code=(int.from_bytes(data[position//8:position//8+3],'little')>>(position%8))&((1<<width)-1)
        position+=width
        if code==clear:
            dictionary={i:bytes([i]) for i in range(clear)};width=minimum+1;next_code=end+1;previous=None;continue
        if code==end:
            if len(output)!=limit:raise ValueError('Decoded frame length differs')
            return bytes(output)
        entry=dictionary.get(code)
        if entry is None:
            if previous is None or code!=next_code:raise ValueError('Invalid LZW code')
            entry=previous+previous[:1]
        output+=entry
        if len(output)>limit:raise ValueError('Frame exceeds declared bounds')
        if previous is not None and next_code<4096:
            dictionary[next_code]=previous+entry[:1];next_code+=1
            if next_code==1<<width and width<12:width+=1
        previous=entry
    raise ValueError('LZW end code absent')


def encode(data,minimum):
    clear=1<<minimum;end=clear+1;width=minimum+1;next_code=end+1
    dictionary={bytes([i]):i for i in range(clear)}
    result=bytearray();bits=0;count=0
    def put(code,size):
        nonlocal bits,count
        bits|=code<<count;count+=size
        while count>=8:result.append(bits&255);bits>>=8;count-=8
    put(clear,width)
    previous=bytes([data[0]])
    for item in data[1:]:
        value=bytes([item]);combined=previous+value
        if combined in dictionary:previous=combined;continue
        put(dictionary[previous],width)
        if next_code<4096:
            dictionary[combined]=next_code;next_code+=1
            if next_code>1<<width and width<12:width+=1
        else:
            put(clear,width);dictionary={bytes([i]):i for i in range(clear)};width=minimum+1;next_code=end+1
        previous=value
    put(dictionary[previous],width)
    if next_code==1<<width and width<12:width+=1
    put(end,width)
    if count:result.append(bits&255)
    return bytes(result)


def _repack(blob):
    if blob[:6] not in (b'GIF87a',b'GIF89a'):raise ValueError('Not GIF')
    header=bytearray(blob[:13]);flags=header[10]
    if not flags&128:raise ValueError('Global palette required')
    global_count=1<<((flags&7)+1);global_palette=blob[13:13+3*global_count]
    position=13+3*global_count;chunks=[];frames=[];pending=None
    global_used={header[11]}
    while position<len(blob):
        start=position;marker=blob[position];position+=1
        if marker==0x3b:break
        if marker==0x21:
            label=blob[position];position+=1
            while True:
                size=blob[position];position+=1
                if not size:break
                position+=size
            block=bytearray(blob[start:position]);chunks.append(block)
            if label==0xf9:pending=block
        elif marker==0x2c:
            descriptor=bytearray(blob[start:position+9]);position+=9
            fw,fh=struct.unpack_from('<HH',descriptor,5)
            local_count=1<<((descriptor[9]&7)+1) if descriptor[9]&128 else 0
            palette=blob[position:position+3*local_count] if local_count else global_palette
            position+=3*local_count;minimum=blob[position];position+=1
            stream=bytearray()
            while True:
                size=blob[position];position+=1
                if not size:break
                stream+=blob[position:position+size];position+=size
            pixels=decode(bytes(stream),minimum,fw*fh)
            used=set(pixels)
            if pending is not None and pending[3]&1:used.add(pending[6])
            record=dict(descriptor=descriptor,palette=palette,local=bool(local_count),pixels=pixels,used=used,gce=pending)
            if not local_count:global_used|=used
            chunks.append(record);frames.append(record);pending=None
        else:raise ValueError('Unexpected GIF marker')
    if not frames:raise ValueError('No frames')
    def compact(palette,used):
        order=sorted(used)
        if max(order)*3+3>len(palette):raise ValueError('Palette index outside table')
        power=max(1,(len(order)-1).bit_length());size=1<<power
        result=b''.join(palette[i*3:i*3+3] for i in order).ljust(size*3,b'\0')
        return result,{old:new for new,old in enumerate(order)},power
    palette,mapping,power=compact(global_palette,global_used)
    header[10]=(header[10]&0xf8)|(power-1);header[11]=mapping[header[11]]
    output=bytearray(header+palette)
    # GCE blocks occur before images, so rewrite them before emitting chunks.
    for frame in frames:
        if frame['local']:
            frame['new_palette'],frame['mapping'],frame['power']=compact(frame['palette'],frame['used'])
        else:frame['mapping']=mapping;frame['power']=power
        gce=frame['gce']
        if gce is not None and gce[3]&1:gce[6]=frame['mapping'][gce[6]]
    for chunk in chunks:
        if isinstance(chunk,bytearray):output+=chunk;continue
        descriptor=chunk['descriptor']
        if chunk['local']:descriptor[9]=(descriptor[9]&0xf8)|(chunk['power']-1)
        output+=descriptor
        if chunk['local']:output+=chunk['new_palette']
        pixels=bytes(chunk['mapping'][i] for i in chunk['pixels'])
        minimum=max(2,chunk['power']);stream=encode(pixels,minimum)
        if decode(stream,minimum,len(pixels))!=pixels:raise ValueError('LZW roundtrip failed')
        output.append(minimum)
        for offset in range(0,len(stream),255):part=stream[offset:offset+255];output.append(len(part));output+=part
        output.append(0)
    output.append(0x3b)
    return bytes(output)


def optimize(blob):
    from resource_manager import gif_info
    gif_info(blob)
    try:
        result=_repack(blob)
    except (IndexError,KeyError,struct.error) as error:
        raise ValueError('Malformed GIF structure') from error
    if gif_info(result)!=gif_info(blob):
        raise ValueError('Repacked GIF metadata differs')
    return result if len(result)<len(blob) else blob
