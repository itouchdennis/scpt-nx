#!/usr/bin/env python3
"""UnrealScript bytecode of a dumped function: DUMP=dir python3 -I tools/ue/dis.py CLASS FUNCTION

Pandora Tomorrow's tokens are UE2's with 4 byte object pointers and FName indices; 0x44 is a delegate property,
0x45 a delegate assignment, 0x60-0x6F an extended native, 0x70 and up a native by number.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue import classes, clsname, fields, full, names, nm, objs, outer, raw, u  # noqa: E402


def objn(p): return full(p).split('.')[-1] if p in objs else f'?{p:08X}'
def name(i): return names.get(i, f'n{i:X}')


class Dis:
    def __init__(s, data): s.d, s.i = data, 0
    def b(s): s.i += 1; return s.d[s.i - 1]
    def w(s): s.i += 2; return s.d[s.i - 2] | s.d[s.i - 1] << 8
    def dw(s): s.i += 4; return int.from_bytes(s.d[s.i - 4:s.i], 'little')

    def args(s):
        a = []
        while s.i < len(s.d) and s.d[s.i] != 0x16:
            a.append(s.expr())
        s.i += 1
        return ', '.join(a)

    def expr(s):
        t = s.b()
        if t in (0, 1, 2): return ['Local', 'Inst', 'Default'][t] + '(' + objn(s.dw()) + ')'
        if t == 0x04: return 'return ' + s.expr()
        if t == 0x05: s.b(); return f'switch({s.expr()})'
        if t == 0x06: return f'jump {s.w():04X}'
        if t == 0x07: o = s.w(); return f'if !({s.expr()}) jump {o:04X}'
        if t == 0x0A: o = s.w(); return 'default:' if o == 0xFFFF else f'case {s.expr()}: next {o:04X}'
        if t == 0x0B: return 'nothing'
        if t in (0x0F, 0x14, 0x45): l = s.expr(); return f'{l} = {s.expr()}'
        if t in (0x10, 0x1A): i = s.expr(); return f'{s.expr()}[{i}]'
        if t in (0x12, 0x19): o = s.expr(); s.w(); s.b(); return f'{o}.{s.expr()}'
        if t == 0x13: c = s.dw(); return f'class<{nm(c)}>({s.expr()})'
        if t == 0x16: return ')'
        if t == 0x17: return 'self'
        if t == 0x18: s.w(); return s.expr()
        if t == 0x1B: n = s.dw(); return f'{name(n)}({s.args()})'
        if t == 0x1C: f = s.dw(); return f'{nm(f)}({s.args()})'
        if t == 0x1D: s.i += 4; return str(struct.unpack_from('<i', s.d, s.i - 4)[0])
        if t == 0x1E: s.i += 4; return f'{struct.unpack_from("<f", s.d, s.i - 4)[0]:g}'
        if t == 0x1F: e = s.d.index(0, s.i); v = s.d[s.i:e].decode('latin1'); s.i = e + 1; return repr(v)
        if t == 0x20: return 'obj ' + objn(s.dw())
        if t == 0x21: return 'name ' + name(s.dw())
        if t in (0x22, 0x23): s.i += 12; return 'rot' if t == 0x22 else 'vect'
        if t == 0x24: return f'byte {s.b()}'
        if t in (0x25, 0x26): return str(t - 0x25)
        if t in (0x27, 0x28): return 'true' if t == 0x27 else 'false'
        if t == 0x2A: return 'None'
        if t == 0x2C: return str(s.b())
        if t == 0x2D: return s.expr()
        if t == 0x2E: c = s.dw(); return f'{nm(c)}({s.expr()})'
        if t in (0x32, 0x33): s.dw(); a = s.expr(); return f'({a} {"==" if t == 0x32 else "!="} {s.expr()})'
        if t == 0x34:
            v = ''
            while (c := s.w()): v += chr(c)
            return repr(v)
        if t == 0x36: m = s.dw(); return f'{s.expr()}.{nm(m)}'
        if t == 0x37: return f'{s.expr()}.length'
        if t == 0x38: n = s.dw(); return f'global.{name(n)}({s.args()})'
        if t == 0x42: s.b(); n = s.dw(); return f'delegatecall {name(n)}({s.args()})'
        if t == 0x44: return 'delegate ' + name(s.dw())
        if 0x39 <= t <= 0x5F: return f'cast{t:02X}({s.expr()})'
        if 0x60 <= t < 0x70: return f'native{((t - 0x60) << 8) | s.b()}({s.args()})'
        if t >= 0x70: return f'native{t}({s.args()})'
        return f'<token {t:02X}>'


def disassemble(func):
    p, n = u(func, 0x44), u(func, 0x48)
    d, lines = Dis(raw[p:p + n]), []
    while d.i < n:
        at = d.i
        try:
            lines.append(f'{at:04X}: {d.expr()}')
        except Exception as e:  # an unknown token: stop at it
            lines.append(f'{at:04X}: !! {e}')
            break
    return lines


if __name__ == '__main__':
    c = classes[sys.argv[1]]
    for f in fields(c):
        if nm(f) == sys.argv[2] and clsname(f) == 'Function' and outer(f) == c:
            print(f'== {sys.argv[1]}.{sys.argv[2]} {f:08X}')
            print('\n'.join(disassemble(f)))
