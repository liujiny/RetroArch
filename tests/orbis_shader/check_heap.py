#!/usr/bin/env python3
"""Verify that the linked process params select the strong system-library heap."""
import struct, subprocess, sys
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
assert b[:4]==b"\x7fELF"
phoff=struct.unpack_from('<Q',b,32)[0]
ents,n=struct.unpack_from('<HH',b,54)
ph=[struct.unpack_from('<IIQQQQQQ',b,phoff+i*ents) for i in range(n)]
def u64(addr):
 for p in ph:
  if p[0]==1 and p[3]<=addr and addr+8<=p[3]+p[5]:
   return struct.unpack_from('<Q',b,p[2]+addr-p[3])[0]
 raise AssertionError(hex(addr))
syms={}
for line in subprocess.check_output(['nm',sys.argv[1]],text=True).splitlines():
 s=line.split()
 if len(s)==3: syms[s[2]]=(int(s[0],16),s[1])
heap,kind=syms['sceLibcHeapSize']; libc=syms['_sceLibcParam'][0]; proc=syms['_sceProcessParam'][0]
assert kind=='D',kind
assert u64(heap)==256*1024*1024
assert u64(proc+56)==libc
assert u64(libc+16)==heap
assert u64(u64(libc+32))==1
print('PASS: strong 256 MiB heap referenced by libc and process parameters; extended allocation retained')
