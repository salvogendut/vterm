#!/bin/bash
# Build a bootable Amstrad CPC CP/M Plus disk with VTERM.COM on it.
#
# The CPC CP/M disks are CPC DSK format, which iDSK handles natively (unlike
# the PCW), so this just copies a CP/M Plus boot disk and adds VTERM.COM in raw
# mode (-t 2, a .COM has no AMSDOS header). Boot it (`|cpm`) and run `vterm`.
#
# Env: CPC_BOOT_DISK (source CP/M Plus .dsk), COM (VTERM.COM), OUT (output).
set -eu

HERE="$(cd "$(dirname "$0")/.." && pwd)"
IDSK="${IDSK:-$HERE/../idsk/iDSK}"
CPC_BOOT_DISK="${CPC_BOOT_DISK:?set CPC_BOOT_DISK to a CPC CP/M Plus boot .dsk}"
COM="${COM:-$HERE/build/VTERM.COM}"
OUT="${OUT:-$HERE/dist/vterm-cpc.dsk}"

mkdir -p "$(dirname "$OUT")"
cp "$CPC_BOOT_DISK" "$OUT"
"$IDSK" "$OUT" -i "$COM" -t 2 -f >/dev/null
echo "built $OUT:"
"$IDSK" "$OUT" -l | grep -i vterm || true
