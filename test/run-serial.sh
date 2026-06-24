#!/bin/bash
# Headless serial-passthrough test for vterm.
#
# Boots CP/M Plus on the 1985 PCW emulator, runs vterm from drive B with the
# serial line wired to a PTY, attaches an echo peer, types "PING-1234", and
# captures a screenshot. Because vterm does not echo locally, the text only
# appears if it made the full serial round trip (TX out, echoed, RX back).
#
# Runs the emulator AND the echo peer together (they must share a PTY
# namespace), so invoke this whole script inside the same environment -- e.g.
#   distrobox enter <box> -- bash test/run-serial.sh
#
# Override via environment:
#   EMU         path to the 1985 emulator binary   (default ../1985-testing/1985)
#   BOOT_DISK   CP/M Plus PCW boot .dsk for drive A
#   DISK        vterm data disk for drive B         (default dist/vterm.dsk)
#   SHOT        screenshot output path              (default build/vterm-serial.ppm)
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HERE/../1985-testing/1985}"
BOOT_DISK="${BOOT_DISK:-$HOME/Documents/CPM Boot/CPM3 1-07.dsk}"
DISK="${DISK:-$HERE/dist/vterm.dsk}"
SHOT="${SHOT:-$HERE/build/vterm-serial.ppm}"
CONF="$HERE/build/serial-test.conf"
PEER="$HERE/test/echo_peer.py"
LINK=/tmp/vterm-serial-test
EMU_LOG=/tmp/vterm-emu.log

mkdir -p "$HERE/build"
rm -f "$LINK" "$SHOT"

# Generate the emulator config. PerryFi is OFF so channel-A serial routes to
# the PTY backend (the emulator checks PerryFi before the PTY serial on RX).
cat > "$CONF" <<EOF
[machine]
model = 8256
memory_kb = 256
[storage]
drive_a = $BOOT_DISK
drive_b =
[display]
scale = 1
monochrome = green
video_mode = pcw
[extensions]
ext_second_drive        = true
ext_sanpollo_backplane  = true
ext_serial              = true
ext_serial_backend      = pty
ext_serial_pty_link     = $LINK
ext_perryfi             = false
[advanced]
tinker = true
EOF

# Launch the emulator. Leading newlines + ONE_K_PASTE_GAP delay the keystrokes
# until after CP/M Plus has booted (see docs/BUILD.md).
ONE_K_PASTE_GAP=60 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$EMU" --config "$CONF" --disk-b "$DISK" \
  --paste "$(printf '\n\n\n\n\n\n\n\nb:\nvterm\nPING-1234\n')" \
  --screenshot-at 2200:"$SHOT" --exit-after 2250 2>"$EMU_LOG" &
EMUPID=$!

# Wait (bounded spin, no sleep) for the emulator to create the PTY alias.
for _ in $(seq 1 5000000); do [ -e "$LINK" ] && break; done

python3 "$PEER" "$LINK" 130 &
PEERPID=$!

wait $EMUPID
kill $PEERPID 2>/dev/null

grep -i 'serial' "$EMU_LOG" | head -2
echo "screenshot: $SHOT"
