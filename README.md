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
  screen model (glyphs + per-cell attributes, cursor, scroll region). Covered
  by host unit tests (`make test-vt100`, 32 checks) and cross-compiles for
  Z80. Not yet wired into the live display — that's the renderer.

The renderer (screen model → local CP/M console) is next (see
[Roadmap](#roadmap)).

## Prerequisites

These live as sibling directories next to this repo:

| Path | What it is |
|------|------------|
| `../sdcc` | SDCC 4.5+ with Z80 support (`sdcc`, `sdasz80`, `makebin`) |
| `../idsk` | [iDSK](https://github.com/cpcsdk/idsk) for writing `.dsk` images |
| `../1985-testing` | Amstrad PCW emulator (CP/M Plus) — for headless testing |
| `../1984` | Amstrad CPC emulator (CP/M 2.2 / Plus) — alternative target |

The test targets run the (SDL3) emulator inside the `my-distrobox` container,
which has SDL3; override with `make CONTAINER=<name>`. Override any path on the
make command line, e.g. `make SDCC_BIN=/opt/sdcc/bin EMU=../1985/1985`.

## Build

```bash
make           # -> build/VTERM.COM   (a CP/M .COM image, code at 0x0100)
make disk      # -> dist/vterm.dsk    (VTERM.COM on a CP/M-readable .dsk)
make test-pcw     # headless: boot CP/M Plus, run vterm, screenshot
make test-serial  # headless serial round-trip test (vterm + echo peer)
make clean
```

`build/VTERM.COM` is a plain CP/M program: copy it to any CP/M system and run
`VTERM`. `dist/vterm.dsk` is a CPC DATA-format floppy image containing just
`VTERM.COM`; both `1984` and `1985` read it as a data drive (mount it as drive
B and `B:` / `VTERM`).

## Testing on the emulators

Headless (used by `make test-pcw`): mount the disk as drive B on the PCW,
which auto-boots CP/M Plus from the system disk configured in
`~/.config/1985/1985.conf`, then auto-type the command and grab a screenshot.

Interactively, just launch the emulator with `--disk-b dist/vterm.dsk`, and at
the CP/M prompt type `B:` then `VTERM`.

See [docs/BUILD.md](docs/BUILD.md) for the toolchain details (crt0, the BDOS
calling convention, `.COM` packaging, and the emulator paste-timing trick).

## Source layout

| File | Role |
|------|------|
| `src/crt0.s` | CP/M C runtime startup: entry at `0x0100`, stack at top of TPA, `gsinit`, warm-boot on exit |
| `src/bdos.s` | BDOS call gateway in asm (matches SDCC's `sdcccall(1)`) |
| `src/cpm.h` / `src/cpm.c` | BDOS function constants and console helpers (`conout`, `conin`, `conkey`, `constat`, `prints`) |
| `src/serial.h` / `src/serial.c` | Serial transport interface + CPS8256 Z80-DART backend (`serial_getc`, `serial_putc`, …) |
| `src/cps_io.s` | Z80-DART port access (`in`/`out` on `0xE0`/`0xE1`) |
| `src/vt100.h` / `src/vt100.c` | Portable VT100/ANSI engine: `vt_putc` drives an 80×24 screen model |
| `src/main.c` | Terminal loop (currently raw serial passthrough) |
| `test/` | VT100 host unit tests + headless serial test (echo peer + emulator) |

## Roadmap

1. ~~**Serial transport.**~~ Done — `serial.h` seam with a direct CPS8256
   Z80-DART backend (non-blocking poll of ports `0xE0`/`0xE1`); baud is left to
   `SETSIO`/firmware. A BDOS-AUX backend could be added behind the same
   interface for portability.
2. ~~**VT100 parser + screen model.**~~ Done — `vt100.[ch]`, a host-testable
   state machine maintaining an 80×24 screen model. Subset: text with
   deferred last-column wrap, C0 controls, cursor moves / CUP / CHA / VPA, ED,
   EL, SGR attributes, DECSTBM scroll region, IND/RI/NEL, DECSC/DECRC.
3. **Renderer.** Walk the screen model and paint the local CP/M console
   (diff against a shadow buffer; emit the console's cursor-addressing codes).
   Wire it into the `serial → console` path in `main.c`.

## License

GPL-2.0 (matches the surrounding tooling). © 2026 Salvatore Bognanni.
