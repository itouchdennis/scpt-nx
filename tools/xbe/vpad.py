#!/usr/bin/env python3
"""A virtual Xbox 360 pad (uinput) that reads button names from a FIFO and presses them.

vpad.py FIFO      then: echo A > FIFO   (A B X Y START BACK LB RB LS RS LT RT UP DOWN LEFT RIGHT, 'sleep N',
                  'quit')
"""
import os
import sys
import time

from evdev import AbsInfo, UInput, ecodes as e

BTN = {'A': e.BTN_SOUTH, 'B': e.BTN_EAST, 'X': e.BTN_WEST, 'Y': e.BTN_NORTH, 'START': e.BTN_START,
       'BACK': e.BTN_SELECT, 'LB': e.BTN_TL, 'RB': e.BTN_TR, 'LS': e.BTN_THUMBL, 'RS': e.BTN_THUMBR}
TRIG = {'LT': e.ABS_Z, 'RT': e.ABS_RZ}
HAT = {'UP': (e.ABS_HAT0Y, -1), 'DOWN': (e.ABS_HAT0Y, 1), 'LEFT': (e.ABS_HAT0X, -1), 'RIGHT': (e.ABS_HAT0X, 1)}
stick = AbsInfo(0, -32768, 32767, 16, 128, 0)
cap = {
    e.EV_KEY: list(BTN.values()) + [e.BTN_MODE, e.BTN_THUMBL, e.BTN_THUMBR],
    e.EV_ABS: [(e.ABS_X, stick), (e.ABS_Y, stick), (e.ABS_RX, stick), (e.ABS_RY, stick),
               (e.ABS_Z, AbsInfo(0, 0, 255, 0, 0, 0)), (e.ABS_RZ, AbsInfo(0, 0, 255, 0, 0, 0)),
               (e.ABS_HAT0X, AbsInfo(0, -1, 1, 0, 0, 0)), (e.ABS_HAT0Y, AbsInfo(0, -1, 1, 0, 0, 0))],
}
ui = UInput(cap, name='Microsoft X-Box 360 pad', vendor=0x045e, product=0x028e, version=0x110, bustype=e.BUS_USB)
fifo = sys.argv[1]
print('ready', flush=True)
while True:
    with open(fifo) as f:
        for line in f:
            for name in line.split():
                if name == 'quit':
                    ui.close()
                    sys.exit()
                name = name.upper()
                if name in BTN:
                    ui.write(e.EV_KEY, BTN[name], 1); ui.syn(); time.sleep(0.15)
                    ui.write(e.EV_KEY, BTN[name], 0); ui.syn(); time.sleep(0.3)
                elif name in TRIG:
                    ui.write(e.EV_ABS, TRIG[name], 255); ui.syn(); time.sleep(0.15)
                    ui.write(e.EV_ABS, TRIG[name], 0); ui.syn(); time.sleep(0.3)
                elif name in HAT:
                    ax, v = HAT[name]
                    ui.write(e.EV_ABS, ax, v); ui.syn(); time.sleep(0.15)
                    ui.write(e.EV_ABS, ax, 0); ui.syn(); time.sleep(0.3)
                elif name.startswith('STICK'):
                    # STICK:<L|R>:<x>:<y>:<seconds>, x y in -1..1 (y up positive)
                    _, side, sx, sy, dur = name.split(':')
                    ax, ay = (e.ABS_X, e.ABS_Y) if side == 'L' else (e.ABS_RX, e.ABS_RY)
                    ui.write(e.EV_ABS, ax, int(float(sx) * 32767)); ui.write(e.EV_ABS, ay, int(-float(sy) * 32767)); ui.syn()
                    time.sleep(float(dur))
                    ui.write(e.EV_ABS, ax, 0); ui.write(e.EV_ABS, ay, 0); ui.syn(); time.sleep(0.2)
                elif name.startswith('SLEEP'):
                    time.sleep(float(name[5:] or 1))
