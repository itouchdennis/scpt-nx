#!/usr/bin/env python3
"""Indexes a guest memory dump (F11 on Linux: guest_00000000.bin) of Pandora Tomorrow's Unreal Engine 2.

  index.py DUMPDIR     writes DUMPDIR/names.pkl (FName index -> text) and DUMPDIR/objs.pkl (object -> (name, class))
The tables are the engine's own: GNames TArray at 0x4D4F54, GObjects TArray at 0x4D9018 (another game: find them as
described in docs/LESSONS.md). Reads only your own dump; nothing of the game is kept in the repository.
"""
import pickle
import struct
import sys

GNAMES, GOBJECTS = 0x4D4F54, 0x4D9018

d = sys.argv[1]
raw = open(d + '/guest_00000000.bin', 'rb').read()
data, n = struct.unpack_from('<II', raw, GNAMES)
names = {}
for i in range(n):
    e = struct.unpack_from('<I', raw, data + i * 4)[0]
    if 0x10000 < e < len(raw) - 16:  # FNameEntry: index, flags, hash next, ANSI text
        names[i] = raw[e + 12:e + 76].split(b'\0')[0].decode('latin1')
data, n = struct.unpack_from('<II', raw, GOBJECTS)
objs = {}
for i in range(n):
    o = struct.unpack_from('<I', raw, data + i * 4)[0]
    if 0x10000 < o < len(raw) - 0x40:  # UObject: +0x20 Name, +0x24 Class
        objs[o] = struct.unpack_from('<II', raw, o + 0x20)
pickle.dump(names, open(d + '/names.pkl', 'wb'))
pickle.dump(objs, open(d + '/objs.pkl', 'wb'))
print(len(names), 'names,', len(objs), 'objects')
