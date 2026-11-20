#!/usr/bin/env python3
"""Small helpers for reading an XBE while porting (no game data in here: it reads your own file).

  xbetool.py info XBE                    title, libraries, sections
  xbetool.py dis XBE VA LEN              objdump of LEN bytes at VA (hex)
  xbetool.py find XBE SECTION HEXBYTES   addresses of a byte pattern ('..' matches any byte)
  xbetool.py callers XBE VA              call/jmp rel32 sites that reach VA
"""
import re
import struct
import subprocess
import sys
import tempfile
import os


class Xbe:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        b = self.b
        self.base = struct.unpack_from('<I', b, 0x104)[0]
        nsec, sec_va = struct.unpack_from('<II', b, 0x11C)
        self.secs = []
        for i in range(nsec):
            o = sec_va - self.base + i * 56
            fl, va, vs, ra, rs, nva = struct.unpack_from('<IIIIII', b, o)
            nm = b[nva - self.base:nva - self.base + 16].split(b'\0')[0].decode(errors='replace')
            self.secs.append((nm, va, vs, ra, rs, fl))
        self.end = max(va + vs for _, va, vs, _, _, _ in self.secs)

    def read(self, va, n):
        for nm, sva, vs, ra, rs, fl in self.secs:
            if sva <= va < sva + vs:
                o = va - sva
                data = self.b[ra + o:ra + min(o + n, rs)]
                return data + b'\0' * (min(n, sva + vs - va) - len(data))
        return b''

    def section(self, va):
        for s in self.secs:
            if s[1] <= va < s[1] + s[2]:
                return s
        return None

    def named(self, name):
        return next(s for s in self.secs if s[0] == name)


def disasm(x, va, n):
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(x.read(va, n))
        p = f.name
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', 'intel', f'--adjust-vma={va:#x}', p],
                         capture_output=True, text=True).stdout
    os.unlink(p)
    return [l for l in out.splitlines()[7:]]


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    x = Xbe(path)
    if cmd == 'info':
        cert = struct.unpack_from('<I', x.b, 0x118)[0] - x.base
        tid = struct.unpack_from('<I', x.b, cert + 8)[0]
        name = x.b[cert + 12:cert + 92].decode('utf-16le').split('\0')[0]
        print(f'"{name}" title {tid:08X} base {x.base:08X}')
        nlib, lib_va = struct.unpack_from('<II', x.b, 0x160)
        for i in range(nlib):
            o = lib_va - x.base + i * 16
            n = x.b[o:o + 8].split(b'\0')[0].decode(errors='replace')
            print('  lib', n, '.'.join(str(v) for v in struct.unpack_from('<HHH', x.b, o + 8)))
        for s in x.secs:
            print(f'  {s[0]:10} {s[1]:08X} +{s[2]:X} flags {s[5]:X}')
    elif cmd == 'dis':
        print('\n'.join(disasm(x, int(sys.argv[3], 16), int(sys.argv[4], 16))))
    elif cmd == 'find':
        s = x.named(sys.argv[3])
        body = x.read(s[1], s[2])
        rx = re.compile(re.sub(r'\.\.', '.', ''.join('\\x' + sys.argv[4][i:i + 2] if sys.argv[4][i:i + 2] != '..'
                                                       else '..' for i in range(0, len(sys.argv[4]), 2))).encode(),
                        re.S)
        for m in rx.finditer(body):
            print(f'{s[1] + m.start():08X}')
    elif cmd == 'callers':
        t = int(sys.argv[3], 16)
        for nm, va, vs, ra, rs, fl in x.secs:
            if not fl & 4:
                continue
            b = x.read(va, vs)
            for i in range(len(b) - 5):
                if b[i] in (0xE8, 0xE9) and (va + i + 5 + struct.unpack_from('<i', b, i + 1)[0]) & 0xFFFFFFFF == t:
                    print(f'{nm} {va + i:08X} {"call" if b[i] == 0xE8 else "jmp"}')


if __name__ == '__main__':
    main()
