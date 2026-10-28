#!/usr/bin/env python3
"""Debug: hex dump of ADDR (hex) [LEN hex] in the running game (sacredfire), with strings behind pointers."""
import struct, subprocess, sys
pid=int(subprocess.check_output(['pgrep','-x','sacredfire']).split()[0])
mem=open(f'/proc/{pid}/mem','rb')
def rd(a,n):
    mem.seek(a); return mem.read(n)
def s(a):
    try:
        b=rd(a,48).split(b'\0')[0]
        return b.decode() if b and all(32<=c<127 for c in b) else None
    except (OSError,ValueError): return None
a=int(sys.argv[1],16); n=int(sys.argv[2],16) if len(sys.argv)>2 else 0x100
d=rd(a,n)
for off in range(0,n,8):
    v=struct.unpack_from('<Q',d,off)[0]
    lo,hi=struct.unpack_from('<ii',d,off); fl=struct.unpack_from('<ff',d,off)
    extra=''
    if 0x100000000<v<0x7fffffffffff:
        st=s(v); extra=f' -> {st!r}' if st else ''
    print(f'+{off:#05x}: {v:#018x}  i({lo},{hi}) f({fl[0]:.3g},{fl[1]:.3g}){extra}')
