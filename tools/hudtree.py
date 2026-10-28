#!/usr/bin/env python3
"""Debug: walks a GUI placeable tree from ROOT (hex) in the running game: name (+0x28), sprite name, source."""
import struct, subprocess, sys
pid=int(subprocess.check_output(['pgrep','-x','sacredfire']).split()[0])
mem=open(f'/proc/{pid}/mem','rb')
def rd(a,n):
    mem.seek(a); return mem.read(n)
def q(a): return struct.unpack('<Q',rd(a,8))[0]
def i32(a): return struct.unpack('<i',rd(a,4))[0]
def s(a):
    if not a: return None
    try: return rd(a,64).split(b'\0')[0].decode('latin1')
    except OSError: return '?'
root=int(sys.argv[1],16)
def walk(p,d,out):
    if d>12 or len(out)>400: return
    try:
        name=s(q(p+0x28)); src=q(p+0x18)
        h=q(p+0x38); spr=None
        if h:
            hv=i32(h)
            o=q(h+0x28)
            if o and not (hv & 0x10000000): spr=s(q(o+0x10)) if not (hv&0x20000000) else 'indirect'
        out.append(('  '*d)+f'{p:#x} name={name!r} sprite={spr!r} src={src:#x}')
        lst=q(p+0x48)
        n=i32(lst+0xc) if lst else 0
        items=q(lst+0x20) if lst else 0
        for i in range(min(n,64)): walk(q(items+8*i),d+1,out)
    except OSError as e: out.append(('  '*d)+f'{p:#x} unreadable')
out=[]; walk(root,0,out); print('\n'.join(out[:150])); print(len(out),'nodes')
