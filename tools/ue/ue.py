"""Helpers over an indexed dump (index.py): DUMP=dir python3 -I -c 'import sys; sys.path.insert(0, "tools/ue"); ...'

Layouts (Pandora Tomorrow, UE2 on XDK 5788): UObject +0x18 Outer, +0x1C flags, +0x20 Name, +0x24 Class; UField +0x30
Next; UStruct +0x2C SuperField, +0x3C Children, +0x40 PropertiesSize; UFunction script TArray +0x44/+0x48, +0x60 flags,
+0x64 ParmsSize | ReturnValueOffset << 16, +0x68 Func; UProperty +0x38 ArrayDim/ElementSize, +0x3C flags, +0x44 Offset.
"""
import os
import pickle
import struct

D = os.environ.get('DUMP', '.')
raw = open(D + '/guest_00000000.bin', 'rb').read()
names = pickle.load(open(D + '/names.pkl', 'rb'))
objs = pickle.load(open(D + '/objs.pkl', 'rb'))


def u(a, o=0): return struct.unpack_from('<I', raw, a + o)[0]
def f32(a, o=0): return struct.unpack_from('<f', raw, a + o)[0]
def nm(a): return names.get(objs[a][0], '?') if a in objs else '?'
def cls(a): return objs[a][1] if a in objs else 0
def clsname(a): return nm(cls(a))
def outer(a): return u(a, 0x18)
def super_(c): return u(c, 0x2C)


def full(a):
    p, o = [nm(a)], outer(a)
    while o in objs:
        p.append(nm(o))
        o = outer(o)
    return '.'.join(reversed(p))


classes = {}
for _a in objs:
    if clsname(_a) == 'Class':
        classes.setdefault(nm(_a), _a)


def fields(c):
    out = []
    while c:
        f, seen = u(c, 0x3C), set()
        while f and f not in seen:
            seen.add(f)
            out.append(f)
            f = u(f, 0x30)
        c = super_(c)
    return out


def props(c):  # (name, kind, offset, array dim, object)
    return [(nm(f), clsname(f), u(f, 0x44), u(f, 0x38) & 0xFFFF, f) for f in fields(c) if 'Property' in clsname(f)]


def instances(cname):
    c = classes.get(cname)
    return [a for a in objs if cls(a) == c]


def fstring(a):  # FString: data, count (with the terminator), capacity; UTF-16
    d, n = u(a), u(a, 4)
    return raw[d:d + n * 2].decode('utf-16le', 'ignore').split('\0')[0] if 0 < d < len(raw) and 0 < n < 1000 else ''
