#!/bin/bash
# Build a bootable PCW CP/M Plus disk containing VTERM.COM.
#
# The PCW B: data-disk format is fiddly to synthesise from scratch, so the
# reliable, directly-useful artifact is a *bootable* disk: copy a known-good
# CP/M Plus boot disk, free a little space (drop BASIC.COM), and add VTERM.COM
# with cpmtools. Boot it and run `vterm`.
#
# Needs cpmtools (with the `pcw` diskdef + libdsk) -- run inside the container.
#
# Env: BOOT_DISK (source boot .dsk), COM (VTERM.COM), OUT (output .dsk).
set -eu

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BOOT_DISK="${BOOT_DISK:-$HOME/Documents/CPM Boot/CPM3 1-07.dsk}"
COM="${COM:-$HERE/build/VTERM.COM}"
OUT="${OUT:-$HERE/dist/vterm.dsk}"

mkdir -p "$(dirname "$OUT")"
cp "$BOOT_DISK" "$OUT"
cpmrm -f pcw "$OUT" 0:basic.com 2>/dev/null || true   # free ~28K
cpmrm -f pcw "$OUT" 0:vterm.com 2>/dev/null || true   # replace if present
cpmcp -f pcw "$OUT" "$COM" 0:vterm.com
echo "built $OUT:"
cpmls -f pcw "$OUT" | grep -iE 'vterm|j17cpm3' || true
