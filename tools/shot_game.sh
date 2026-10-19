#!/usr/bin/env bash
# Launch the port, drive it from the title into the loaded household, and screenshot
# the real game - not the intro. The intro is one 2-D quad and proves almost nothing
# about the renderer; every ultrawide, HUD or aspect question has to be answered on a
# 3-D scene with the HUD up.
#
#   tools/shot_game.sh <tag> [build dir]
#
# Screens land in out/shots/<tag>_<what>.png, the game's stdout in out/shots/<tag>.log.
# The walk is the one docs/playtest.md records: the intro movies run ~45 s, then
# START START (title -> main menu), A (Continue -> Select Save), A (Sim 1 ->
# Customize), RIGHT x3 (to the confirm icon), A (save question), A (YES) -> household.
set -uo pipefail
cd "$(dirname "$0")/.."

TAG="${1:?usage: shot_game.sh <tag> [build-dir]}"
BUILD="${2:-out/build/linux-release}"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
export DISPLAY="${DISPLAY:-:0}"
if [ -z "${XAUTHORITY:-}" ]; then
    for x in /run/user/$(id -u)/xauth_*; do [ -e "$x" ] && export XAUTHORITY="$x" && break; done
fi
export XBE_PERF=1 XBE_MUTE=1

mkdir -p out/shots
[ -e "$BUILD/assets" ] || ln -sfn ../../../assets "$BUILD/assets"

LOG="out/shots/${TAG}.log"
: > "$LOG"
"./$BUILD/SimsBustinOut" >> "$LOG" 2>&1 &
PID=$!
trap 'kill -KILL "$PID" 2>/dev/null' EXIT
echo "launched pid $PID"

shot() { spectacle -b -n -f -o "out/shots/${TAG}_$1.png" >/dev/null 2>&1; echo "shot $1"; }
alive() { kill -0 "$PID" 2>/dev/null; }

sleep 18; alive || { echo "died before the intro"; exit 1; }
shot intro

# the intro movies run about 45 s; Start skips to the title
sleep 24; alive || { echo "died during the intro"; exit 1; }
tools/drive_keys.py --delay 1.4 START START >/dev/null 2>&1
sleep 2; shot menu

# Continue -> Select Save -> Sim 1 -> Customize
tools/drive_keys.py --delay 1.6 A A >/dev/null 2>&1
sleep 3; shot saveselect

# right to the confirm icon, accept, then YES
tools/drive_keys.py --delay 1.2 RIGHT RIGHT RIGHT >/dev/null 2>&1
sleep 1; shot cas
tools/drive_keys.py --delay 2.0 A A >/dev/null 2>&1

# the household load takes a while
sleep 35; alive || { echo "died loading the household"; exit 1; }
shot house
sleep 8; shot house2

kill -TERM "$PID" 2>/dev/null; sleep 2; kill -KILL "$PID" 2>/dev/null
trap - EXIT
echo "--- draws/frame seen ---"
grep -o 'per frame [0-9]* draws' "$LOG" | sort -u | tail -5
echo "--- present geometry ---"
grep -o 'vulkan: present .*' "$LOG" | tail -1
