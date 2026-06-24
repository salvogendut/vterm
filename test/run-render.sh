#!/bin/bash
# Headless render test: boot the bootable vterm disk, run vterm from A:, attach
# a VT100-sending peer to the serial PTY, and screenshot. The peer sends a
# VT100 test pattern; vterm parses it and the renderer paints it on the PCW
# (VT52) console.
#
# Run inside the container (emulator + peer share a PTY namespace):
#   distrobox enter <box> -- bash test/run-render.sh
#
# Env overrides: EMU, DISK (bootable, vterm on A:), SHOT, PEER.
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HERE/../1985-testing/1985}"
DISK="${DISK:-$HERE/dist/vterm.dsk}"
SHOT="${SHOT:-$HERE/build/vterm-render.ppm}"
PEER="${PEER:-$HERE/test/vt_peer.py}"
CONF="$HERE/build/render-test.conf"
LINK=/tmp/vterm-serial-test
EMU_LOG=/tmp/vterm-emu.log

mkdir -p "$HERE/build"
rm -f "$LINK" "$SHOT"

cat > "$CONF" <<EOF
[machine]
model = 8256
memory_kb = 256
[storage]
drive_a = $DISK
drive_b =
[display]
monochrome = green
video_mode = pcw
[extensions]
ext_serial          = true
ext_serial_backend  = pty
ext_serial_pty_link = $LINK
ext_perryfi         = false
ext_sanpollo_backplane = true
[advanced]
tinker = true
EOF

# Boot, then run vterm from A: (generous lead-in past boot/disk activity).
ONE_K_PASTE_GAP=70 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$EMU" --config "$CONF" \
  --paste "$(printf '\n\n\n\n\n\n\n\n\n\nvterm\n')" \
  --screenshot-at 2800:"$SHOT" --exit-after 2860 2>"$EMU_LOG" &
EMUPID=$!

for _ in $(seq 1 5000000); do [ -e "$LINK" ] && break; done

python3 "$PEER" "$LINK" 150 &
PEERPID=$!

wait $EMUPID
kill $PEERPID 2>/dev/null
echo "screenshot: $SHOT"
