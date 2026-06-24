# vterm

A VT100 terminal emulator for **CP/M**, written in C and compiled to a `.COM`
with the SDCC Z80 toolchain. It connects to a remote host over a **serial
(RS-232)** line, renders incoming VT100 escape sequences onto the **CP/M
console (via BDOS)**, and sends local keystrokes back out the serial line.

[qterm](https://git.imzadi.de/acn/qterm) is used as a *behavioural* reference
for VT100 handling and terminal UX; vterm is an independent C implementation.

## Status

- **Milestone 1 — toolchain + scaffolding: complete.** SDCC → `.COM` build,
  thin BDOS console layer.
- **Milestone 2 — serial passthrough: complete.** A terminal loop pumps raw
  bytes both ways between the CP/M console and the serial line, driving the
  Amstrad CPS8256 Z80-DART directly for non-blocking I/O. Verified under real
  CP/M Plus on the PCW emulator with an echo peer: typed text makes the full
  TX → serial → RX round trip and appears on screen.
- **Milestone 3 — VT100 parser + screen model: complete.** A portable,
  I/O-free engine (`vt100.[ch]`) parses VT100/ANSI sequences into an 80×24
  screen model. Covered by host unit tests (`make test-vt100`, 32 checks) and
  cross-compiles for Z80.
- **Milestone 4 — renderer + full terminal: complete.** `render.[ch]` diffs
  the screen model against a shadow buffer and paints changed cells onto the
  PCW's VT52 console (`ESC Y` positioning, `ESC p/q` inverse, `ESC r/u`
  bright). `main.c` is now the full terminal loop (serial → VT100 → render;
  keyboard → serial). Verified end-to-end under real CP/M Plus: a VT100 test
  pattern (centred title, inverse/bold runs, a cursor-addressed box) renders
  correctly on the PCW (`make test-render`).

This is a working VT100 terminal. Further work is breadth: a wider VT100/ANSI
subset, configurable console back-ends for other CP/M machines, and UX (status
line, capture, file transfer). See [Roadmap](#roadmap).

## Prerequisites

These live as sibling directories next to this repo:

| Path | What it is |
|------|------------|
| `../sdcc` | SDCC 4.5+ with Z80 support (`sdcc`, `sdasz80`, `makebin`) — host |
| `../1985-testing` | Amstrad PCW emulator (CP/M Plus) — for headless testing |

Building `VTERM.COM` needs only `../sdcc` on the host. The disk and emulator
test targets run inside the `my-distrobox` container, which provides SDL3 (for
the emulator) and `cpmtools` (with the `pcw` diskdef + libdsk, for the disk);
override with `make CONTAINER=<name>`. Override any path on the make command
line, e.g. `make SDCC_BIN=/opt/sdcc/bin EMU=../1985/1985`.

## Build

```bash
make              # -> build/VTERM.COM  (a CP/M .COM image, code at 0x0100)
make disk         # -> dist/vterm.dsk   (bootable PCW CP/M Plus disk + vterm.com)
make test-vt100   # host (gcc) unit tests for the VT100 engine
make test-render  # headless: boot, run vterm, a VT100 peer draws, screenshot
make test-serial  # headless serial round-trip test (vterm + echo peer)
make clean
```

`build/VTERM.COM` is a plain CP/M program: copy it to any CP/M system and run
`VTERM`. `dist/vterm.dsk` is a **bootable** PCW CP/M Plus disk (a boot-disk copy
with `VTERM.COM` added) — boot it on the PCW and type `VTERM`.

## Testing on the emulators

The PCW emulator is an SDL3 app; the test targets build the disk (cpmtools) and
run the emulator headless inside the `my-distrobox` container (`SDL_VIDEODRIVER=
dummy` + `--screenshot-at`). `make test-render` boots the disk, runs `vterm`,
and a VT100 peer on the serial PTY draws a test pattern that the renderer paints
on the PCW console; `make test-serial` does the TX→RX echo round trip.

See [docs/BUILD.md](docs/BUILD.md) for the details: crt0, the BDOS calling
convention, `.COM` packaging, the PCW VT52 console codes, the PCW disk format,
and the emulator paste-timing trick.

## Source layout

| File | Role |
|------|------|
| `src/crt0.s` | CP/M C runtime startup: entry at `0x0100`, stack at top of TPA, `gsinit`, warm-boot on exit |
| `src/bdos.s` | BDOS call gateway in asm (matches SDCC's `sdcccall(1)`) |
| `src/cpm.h` / `src/cpm.c` | BDOS function constants and console helpers (`conout`, `conin`, `conkey`, `constat`, `prints`) |
| `src/serial.h` / `src/serial.c` | Serial transport interface + CPS8256 Z80-DART backend (`serial_getc`, `serial_putc`, …) |
| `src/cps_io.s` | Z80-DART port access (`in`/`out` on `0xE0`/`0xE1`) |
| `src/vt100.h` / `src/vt100.c` | Portable VT100/ANSI engine: `vt_putc` drives an 80×24 screen model |
| `src/render.h` / `src/render.c` | Diff the screen model onto the PCW VT52 console (`ESC Y`, inverse, bright) |
| `src/main.c` | Terminal loop: serial → VT100 → render; keyboard → serial |
| `test/` | VT100 host unit tests, PCW disk builder, and headless emulator tests |

## Roadmap

1. ~~**Serial transport.**~~ Done — `serial.h` seam with a direct CPS8256
   Z80-DART backend (non-blocking poll of ports `0xE0`/`0xE1`); baud is left to
   `SETSIO`/firmware. A BDOS-AUX backend could be added behind the same
   interface for portability.
2. ~~**VT100 parser + screen model.**~~ Done — `vt100.[ch]`, a host-testable
   state machine maintaining an 80×24 screen model. Subset: text with
   deferred last-column wrap, C0 controls, cursor moves / CUP / CHA / VPA, ED,
   EL, SGR attributes, DECSTBM scroll region, IND/RI/NEL, DECSC/DECRC.
3. ~~**Renderer + terminal loop.**~~ Done — `render.[ch]` diffs the model onto
   the PCW VT52 console; `main.c` ties serial, VT100 and render together.
4. **Breadth.** Wider VT100/ANSI coverage (insert/delete line & char,
   tab stops, origin mode, more SGR), a configurable console back-end for other
   CP/M machines (the renderer is PCW-VT52-specific today), `SETSIO`-free DART
   setup for real hardware, and UX (status line, capture, file transfer).

## License

GPL-2.0 (matches the surrounding tooling). © 2026 Salvatore Bognanni.
