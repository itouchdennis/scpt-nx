#!/usr/bin/env bash
# The builder container's job (docker/Dockerfile), started by build-nro-docker.sh. Mounted:
#   /game    your copy of the game, read only: an Xbox disc image (.iso, .xiso) or the extracted disc's folder
#   /out     where the SD card folder is made: /out/switch/scpt/scpt.nro and /out/switch/scpt/game/
#   /cache   kept between runs: the recompiled code and the build (a Docker volume)
#   /icon.jpg  optional, your own 256x256 JPEG for the Homebrew Menu
# SCPT_ANY_RELEASE=1 builds from an Offline.xbe other than the release the port is made from; SCPT_CODE_OPT=-O2 builds
# the game code as before -O3, for comparing.
set -euo pipefail

TESTED_XBE_SHA1=4f4cc03705247cd2b4bfc00260ef60b82681ded6  # Offline.xbe of Pandora Tomorrow (Europe), the release it is made from
NVK=/opt/nvk-sdk/opt/devkitpro/portlibs/switch
DEST=/out/switch/scpt
GAME="$DEST/game"

step() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

[ -e /game ] || fail "no game mounted at /game (build-nro-docker.sh does that for you)"
[ -d /out ] && [ -w /out ] || fail "no writable folder mounted at /out"
[ -d /cache ] && [ -w /cache ] || fail "no writable volume mounted at /cache"

step "Game files -> $GAME"
mkdir -p "$GAME"
if [ -d /game ]; then
    [ -f /game/Offline.xbe ] || fail "the folder you chose has no Offline.xbe: choose the extracted disc's folder"
    # sizes only, no owners or permissions: /out can be a Windows folder (Docker Desktop), which takes neither
    rsync -r --info=progress2 --size-only --exclude '*.iso' /game/ "$GAME/"
elif [ -f "$GAME/Offline.xbe" ] && [ "$GAME/Offline.xbe" -nt /game ]; then
    echo "already extracted"
else
    extract-xiso -s -d "$GAME" -x /game || fail "the file you chose could not be read as an Xbox disc image"
fi
[ -f "$GAME/Offline.xbe" ] || fail "no Offline.xbe in what you chose: is it the Splinter Cell: Pandora Tomorrow disc?"

XBE_SHA1="$(sha1sum "$GAME/Offline.xbe" | cut -d' ' -f1)"
if [ "$XBE_SHA1" = "$TESTED_XBE_SHA1" ]; then
    echo "Offline.xbe: the release the port is made from (Europe)"
elif [ "${SCPT_ANY_RELEASE:-0}" = 1 ]; then
    echo "Offline.xbe: sha1 $XBE_SHA1, not the release the port is made from; building it anyway (SCPT_ANY_RELEASE=1)"
else
    fail "Offline.xbe has sha1 $XBE_SHA1, not the release this port is made from ($TESTED_XBE_SHA1).
Another release may not work. To build it anyway, run again with SCPT_ANY_RELEASE=1."
fi

GEN="/cache/generated/$XBE_SHA1"
step "Recompiling Offline.xbe"
if [ -f "$GEN/image_symbols.h" ] && [ "$GEN/codegen.stamp" -nt /opt/scpt/config/scpt.toml ]; then
    echo "already recompiled"
else
    mkdir -p "$GEN"
    (cd /opt/scpt && xbe_recomp config/scpt.toml "$GAME/Offline.xbe" "$GEN")
    touch "$GEN/codegen.stamp"
fi

step "Building the NRO"
icon=()
[ -f /icon.jpg ] && icon=(SCPT_ICON=/icon.jpg)
env "${icon[@]}" SCPT_GEN_DIR="$GEN" SCPT_NVK_SDK="$NVK" SD_ROOT=/out BUILD_DIR=/cache/build-switch-vk \
    SCPT_CODE_OPT="${SCPT_CODE_OPT:--O3}" /opt/scpt/switch/build.sh
[ -f "$DEST/scpt.nro" ] || fail "no NRO was made"

step "Done"
echo "Copy the folder switch/ from your output folder onto the root of your SD card, so that it holds"
echo "  sdmc:/switch/scpt/scpt.nro and sdmc:/switch/scpt/game/Offline.xbe"
echo "and start it from the Homebrew Menu with title takeover: hold R while starting any game."
echo
echo "The NRO holds code made from your copy of the game: it is for you only, please do not share it."
