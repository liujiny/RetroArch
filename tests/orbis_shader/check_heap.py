#!/usr/bin/env python3
"""Audit heap mode/capacity and CRT pointers in ELF, converted ELF and fSELF."""
import struct
import subprocess
import sys
from pathlib import Path

symbols = {}
for line in subprocess.check_output(['nm', sys.argv[1]], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = (int(fields[0], 16), fields[1])
assert symbols['sceLibcHeapSize'][1] == 'D'
assert symbols['_sceLibcParam'][1] in ('D', 'R')

for path in sys.argv[1:]:
    data = Path(path).read_bytes()
    elf_offset = 0
    entries = None
    if data[:4] == bytes.fromhex('4f153d1d'):
        count = struct.unpack_from('<H', data, 24)[0]
        entries = [struct.unpack_from('<QQQQ', data, 32+i*32) for i in range(count)]
        elf_offset = 32+count*32
    assert data[elf_offset:elf_offset+4] == b'\x7fELF'
    phoff = struct.unpack_from('<Q', data, elf_offset+32)[0]
    entsize, count = struct.unpack_from('<HH', data, elf_offset+54)
    headers = [struct.unpack_from('<IIQQQQQQ', data, elf_offset+phoff+i*entsize) for i in range(count)]

    def u64(address):
        for i, header in enumerate(headers):
            kind, _, offset, va, _, size, _, _ = header
            if kind not in (1, 0x61000010) or not va <= address <= va+size-8:
                continue
            if entries is not None:
                entry = next(e for e in entries if e[0] & (1 << 11) and (e[0] >> 20) & 0xffff == i)
                assert entry[2] == entry[3], 'Compressed SELF is not supported'
                offset = entry[1]
            return struct.unpack_from('<Q', data, offset+address-va)[0]
        raise AssertionError(hex(address))

    heap = symbols['sceLibcHeapSize'][0]
    libc = symbols['_sceLibcParam'][0]
    proc = symbols['_sceProcessParam'][0]
    assert u64(proc+56) == libc
    assert u64(libc) == 0x90
    assert u64(libc+8) == 0x10000000c, 'Preserve CRT version and internal mode'
    assert u64(heap) == 256*1024*1024
    assert u64(libc+16) == heap
    assert u64(libc+0x68) == heap, 'Internal mode needs its own heap-size field'
    assert u64(u64(libc+32)) == 1
    for offset, name in [(0x30, 'sceLibcMallocReplace'), (0x38, 'sceLibcNewReplace'), (0x60, 'sceLibcMallocReplaceForTls')]:
        assert u64(libc+offset) == symbols[name][0], name
    for offset in (0x18, 0x28, 0x40, 0x48, 0x50, 0x58, 0x70, 0x78, 0x80, 0x88):
        assert u64(libc+offset) == 0
    print('PASS:', Path(path).name, 'mode=1; ordinary/internal heap=256MiB; other CRT fields preserved')
