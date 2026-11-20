#!/usr/bin/env bash
# Linux test of a mission view: starts the build under Xvfb :99, presses A three times through the menus with the
# virtual pad on FIFO $PAD (tools/xbe/vpad.py), waits for the mission and takes a screenshot.
#   embassy_shot.sh OUT.png [ENV=VALUE ...]
set -u
out="$1"; shift
dir="$(cd "$(dirname "$0")/../.." && pwd)/build/linux-vk"
pad="${PAD:?the FIFO of the virtual pad}"
pkill -x scda; sleep 2
cd "$dir" && rm -rf logs
env DISPLAY=:99 SDL_VIDEODRIVER=x11 "$@" timeout 400 ./scda > run-shot.out 2>&1 &
sleep 35
for i in 1 2 3; do echo A > "$pad"; sleep 5; done
sleep "${WAIT:-75}"
DISPLAY=:99 magick import -window root -resize 640x "$out"
pkill -x scda
