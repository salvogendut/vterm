# Build & toolchain notes

How vterm is compiled from C into a CP/M `.COM`, packaged onto a disk, and
tested. These notes capture the non-obvious bits so they don't have to be
re-derived.

## Pipeline overview

```
src/*.c  --sdcc-->  build/*.rel  \
src/*.s  --sdas-->  build/*.rel   >-- sdcc (link) --> build/vterm.ihx
                                 /                         |
                                                     makebin -p -o 256
                                                           |
                                                     build/VTERM.COM
                                                           |
                                                  iDSK -i -t 2 (raw)
                                                           |
                                                     dist/vterm.dsk
```

## crt0 (`src/crt0.s`)

Adapted from SDCC's shipped generic Z80 crt0
(`share/sdcc/lib/src/z80n/crt0.s`), retargeted for CP/M:

- **No ABS header.** The image is anchored purely by `--code-loc 0x0100`, and
  crt0 is linked **first**, so its `_CODE` area starts at `0x0100` and `init`
  is the first instruction CP/M executes. (Putting an ABS `_HEADER` at `0x0100`
  *and* relocating `_CODE` to `0x0100` would overlap.)
- **Stack at top of the TPA.** `ld hl,(0x0006)` reads the BDOS entry address
  (top of usable memory) and loads it into `SP`. CP/M only gives a loaded
  program a tiny stack, so we set our own.
- **Exit = warm boot.** Returning from `main()` does `jp 0x0000`.
- The `gsinit` body (zero `_DATA`/`_BSS`, copy `_INITIALIZER` → `_INITIALIZED`)
  and the linker area ordering are taken verbatim from the stock crt0.

## BDOS calling convention (`src/bdos.s`)

This SDCC defaults to **`sdcccall(1)`**. For
`unsigned char bdos(unsigned char func, unsigned int arg)` it passes:

| C parameter | Register |
|-------------|----------|
| `func` (1st, 8-bit) | `A` |
| `arg` (2nd, 16-bit) | `DE` |
| return (8-bit) | `A` |
| return (16-bit, `bdos16`) | `HL` |

CP/M's BDOS wants the function number in `C` and the parameter in `DE`, entered
via `call 5`, returning in `A`/`HL`. So the gateway is just `ld c,a` then
`call 5` then `ret`. (Verified by compiling a probe and reading the generated
asm — do that again if the SDCC version changes.)

## Assembling `.s` files

SDCC's driver rejects `.s` here ("file extension unsupported"), so assemble
with `sdasz80` directly. Two gotchas:

- **`-g` is required.** The crt0 references SDCC-generated externals
  (`l__DATA`, `s__INITIALIZER`, …). Without `-g` ("undefined symbols made
  global") the assembler errors with `<u> undefined symbol`.
- **`-o` takes the object name literally.** `sdasz80 -o build/crt0 …` writes
  `build/crt0.asm` (its default object extension), so pass the full
  `build/crt0.rel`.

## Linking and `.COM` packaging

```
sdcc -mz80 --no-std-crt0 --code-loc 0x0100 --data-loc 0x0000 \
     build/crt0.rel build/bdos.rel build/cpm.rel build/main.rel \
     -o build/vterm.ihx
makebin -p -o 256 build/vterm.ihx build/VTERM.COM
```

`makebin` emits a binary starting at address 0; `-o 256` skips the `0x000–0x0FF`
padding so the image begins at `0x0100` (a valid `.COM`), and `-p` truncates
trailing padding. Sanity check: the first byte of `VTERM.COM` is `0x2A`
(`ld hl,(nn)` — the crt0 stack setup); `build/vterm.map` shows `_CODE = 0x0100`.

## Disk image (`iDSK`)

```
iDSK dist/vterm.dsk -n              # new CPC DATA-format disk
iDSK dist/vterm.dsk -i VTERM.COM -t 2 -f
```

`-t 2` (raw) is essential: a CP/M `.COM` has **no** AMSDOS header, so importing
as `-t 1` (binary) would prepend a 128-byte header and corrupt it. The result
is a CPC DATA-format image; the PCW (`1985`) and CPC (`1984`) both read it as a
data drive. (This mirrors the sibling `WGet` project, whose `WGet-CPC.dsk` is
likewise labelled "CPC,PCW".) iDSK does **not** correctly write PCW-native
system disks, so we mount this as a *data* drive (B:) rather than trying to add
files to a bootable PCW disk.

## Headless test on the PCW emulator (`1985`)

```
ONE_K_PASTE_GAP=60 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ../1985/1985 --disk-b "$(pwd)/dist/vterm.dsk" \
  --paste "$(printf '\n\n\n\n\n\n\n\nb:\nvterm\n')" \
  --screenshot-at 1600:build/vterm-pcw.ppm --exit-after 1650
```

- `SDL_VIDEODRIVER=dummy` runs without a window; `--screenshot-at N:PATH` still
  renders a PPM (convert with `magick`/`ffmpeg` to view).
- **Paste timing:** `--paste` queues keystrokes at frame 0 and injects them
  fast (≈3 frames/char), but PCW CP/M Plus only reaches the `A>` prompt around
  frame ~300. So the real keystrokes must be delayed past boot:
  `ONE_K_PASTE_GAP=60` spaces them ~60 frames apart and the leading newlines
  (harmless empty commands at `A>`) push `b:` / `vterm` to land after boot.
  Tune the newline count / gap if boot time changes.

## Quick reference: bytes of a correct build

```
$ hexdump -C build/VTERM.COM | head -1
00000000  2a 06 00 f9 cd .. 01 cd  .. 01 c3 0d 01 c3 00 00
          └ ld hl,(6) ┘ │  └call gsinit┘ └call main┘ └jp _exit┘
                     ld sp,hl                          (=jp 0x0000)
```
