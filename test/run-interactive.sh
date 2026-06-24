#!/bin/bash
# Interactive vterm session: launch the PCW emulator (GUI) with the serial line
# bridged to a real shell. Boot CP/M, type `vterm`, and you have a VT100
# terminal into a shell (try `ls`, `vi`, etc.). Quit vterm with Ctrl-].
#
# Run inside the container so the emulator and the shell bridge share a PTY
# namespace and the SDL3 the emulator was built against:
#   distrobox enter my-distrobox -- bash test/run-interactive.sh
# or just:  make run
#
# Env overrides: EMU, DISK, SHELL_CMD (default: bash --norc -i).
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HERE/../1985-testing/1985}"
DISK="${DISK:-$HERE/dist/vterm.dsk}"
CONF="$HERE/build/interactive.conf"
LINK=/tmp/vterm-serial
: "${SHELL_CMD:=bash --norc -i}"

mkdir -p "$HERE/build"
rm -f "$LINK"

cat > "$CONF" <<EOF
[machine]
model = 8256
memory_kb = 256
[storage]
drive_a = $DISK
[display]
scale = 2
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

# Bridge the serial PTY to a shell once the emulator creates it.
(
  for _ in $(seq 1 90000000); do [ -e "$LINK" ] && break; done
  exec python3 "$HERE/test/serial_shell.py" "$LINK" $SHELL_CMD
) &
BRIDGE=$!

echo "Booting... when you reach A>, type:  vterm   (then use the shell; Ctrl-] quits vterm)"
"$EMU" --config "$CONF"

kill "$BRIDGE" 2>/dev/null
