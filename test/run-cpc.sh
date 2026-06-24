#!/bin/bash
# Headless CPC render test: boot CP/M Plus on the 1984 emulator, run vterm,
# attach a VT100 peer to the USIfAC serial PTY, and screenshot.
#
# The CPC boots BASIC, then `|cpm` loads CP/M Plus, then `vterm` runs from A:.
# 1984 only accepts the `=` option form (--paste=..., --screenshot-at=N:PATH).
# Run inside the container (emulator + peer share a PTY namespace).
#
# Env: EMU, ROMS, DISK, SHOT, PEER.
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HERE/../1984/1984}"
ROMS="${ROMS:-$HOME/.config/1984/roms}"
DISK="${DISK:-$HERE/dist/vterm-cpc.dsk}"
SHOT="${SHOT:-$HERE/build/vterm-cpc.ppm}"
PEER="${PEER:-$HERE/test/vt_peer.py}"
CONF="$HERE/build/cpc-test.conf"
LINK=/tmp/vterm-cpc-serial
EMU_LOG=/tmp/cpc-emu.log

mkdir -p "$HERE/build"
rm -f "$LINK" "$SHOT"

cat > "$CONF" <<EOF
[machine]
model=6128
memory=128
[roms]
os=$ROMS/OS_6128.ROM
basic=$ROMS/BASIC_1.1.ROM
amsdos=$ROMS/AMSDOS.ROM
[storage]
drive_a=$DISK
[hardware]
mx4=true
rom_board=true
usifac=true
usifac_backend=pty
usifac_pty_link=$LINK
perryfi=false
net4cpc=false
[display]
scale=2
EOF

# |cpm boots CP/M Plus; the filler newlines pace past the BASIC and CP/M
# load before `vterm` is typed.
ONE_K_PASTE_GAP=80 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$EMU" --config="$CONF" \
  --paste="$(printf '\n\n\n|cpm\n\n\n\n\n\n\nvterm\n')" \
  --screenshot-at=3500:"$SHOT" --exit-after=3560 2>"$EMU_LOG" &
EMUPID=$!

for _ in $(seq 1 5000000); do [ -e "$LINK" ] && break; done

python3 "$PEER" "$LINK" 150 &
PEERPID=$!

wait $EMUPID
kill $PEERPID 2>/dev/null
grep -i 'usifac' "$EMU_LOG" | head -1
echo "screenshot: $SHOT"
