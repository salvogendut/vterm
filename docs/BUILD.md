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

## VT100 engine (`src/vt100.[ch]`)

The parser/screen-model is deliberately I/O-free and portable so it builds
with both gcc (host) and SDCC (Z80). `make test-vt100` compiles `vt100.c` with
gcc against `test/vt100_test.c` (32 assertions over text/wrap/cursor/erase/
SGR/scroll/DECSTBM/DECSC), then cross-compiles `vt100.c` with `sdcc -mz80` as a
target build check. The engine takes a caller-allocated `VT` struct (no
malloc); on the target that will be one global. SDCC emits warning 110
("…EVELYN the modified DOG") on the TAB arithmetic — that is SDCC's benign
peephole notice, not a logic issue.

## Serial backend — CPS8256 Z80-DART (`src/cps_io.s`, `src/serial.c`)

The PCW's serial line is channel A of the Z80-DART in the CPS8256 add-on:

| Port | Use |
|------|-----|
| `0xE0` | DART channel A data (read = RX byte, write = TX byte) |
| `0xE1` | DART channel A control: write `WR0` to select a register; read returns the selected `RRx` |
| `0xE4`–`0xE7` | 8253 PIT used as the baud generator (set by `SETSIO`) |

Reading status (`RR0`): write `0` to `0xE1` to point the register pointer at
`RR0`, then read `0xE1`. `RR0` bit 0 = RX char available, bit 2 = TX buffer
empty. **Reading `0xE0` pops a byte unconditionally**, so always check the RX
bit first. We only touch fixed ports, so `cps_io.s` uses immediate-port I/O
(`in a,(0xE0)` / `out (0xE1),a`) — a single argument in `A`, no register
juggling — and the PCW decodes only the low 8 address bits, so the high byte on
the bus is irrelevant. Baud and line setup are left to `SETSIO`/firmware;
`serial_init()` is a no-op.

## Headless test on the PCW emulator (`1985-testing`)

The emulator is an SDL3 app built against the **distrobox container's** SDL3
(`distrobox enter my-distrobox -- … autoreconf -fiv && ./configure && make`),
so the `make test-pcw` / `make test-serial` targets run it inside that
container. The container shares `$HOME` and `/tmp`, so disk paths, the serial
PTY, and the screenshot output are all visible from the host (convert the
`.ppm` with `magick`).

**Serial round-trip test** (`test/run-serial.sh`): boots CP/M Plus, runs vterm
from drive B with the serial line on a PTY, attaches `test/echo_peer.py`, types
`PING-1234`, and screenshots. vterm does **no** local echo, so the text only
appears if it survived TX → serial → echo → RX. One gotcha: the emulator checks
PerryFi before the PTY serial backend on RX, so the generated test config sets
`ext_perryfi=false`.

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
