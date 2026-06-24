# vterm

A VT100 terminal emulator for **CP/M**, written in C and compiled to a `.COM`
with the SDCC Z80 toolchain. It connects to a remote host over a **serial
(RS-232)** line, renders incoming VT100 escape sequences onto the **CP/M
console (via BDOS)**, and sends local keystrokes back out the serial line.

[qterm](https://git.imzadi.de/acn/qterm) is used as a *behavioural* reference
for VT100 handling and terminal UX; vterm is an independent C implementation.

## Status

**Milestone 1 — toolchain + scaffolding: complete.** The SDCC → `.COM` build
works, there is a thin BDOS console layer, and a placeholder `main` proves the
chain end-to-end. Verified running under real CP/M Plus on the PCW emulator
(banner prints, console input echoes, clean exit back to the CP/M prompt).

The serial transport, VT100 parser, and screen model are not implemented yet
(see [Roadmap](#roadmap)).

## Prerequisites

These live as sibling directories next to this repo:

| Path | What it is |
|------|------------|
| `../sdcc` | SDCC 4.5+ with Z80 support (`sdcc`, `sdasz80`, `makebin`) |
| `../idsk` | [iDSK](https://github.com/cpcsdk/idsk) for writing `.dsk` images |
| `../1985` | Amstrad PCW emulator (CP/M Plus) — for headless testing |
| `../1984` | Amstrad CPC emulator (CP/M 2.2 / Plus) — alternative target |

Override any path on the make command line, e.g. `make SDCC_BIN=/opt/sdcc/bin`.

## Build

```bash
make           # -> build/VTERM.COM   (a CP/M .COM image, code at 0x0100)
make disk      # -> dist/vterm.dsk    (VTERM.COM on a CP/M-readable .dsk)
make test-pcw  # headless: boot CP/M Plus on ../1985, run vterm, screenshot
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
| `src/cpm.h` / `src/cpm.c` | BDOS function constants and console helpers (`conout`, `conin`, `constat`, `prints`) |
| `src/main.c` | Scaffold entry point (banner + echo demo); will become the terminal loop |

## Roadmap

1. **Serial transport.** Abstract the link behind a small
   `serial_getc` / `serial_putc` / `serial_status` interface. Start over BDOS
   AUX (functions 3/4) for portability; add a direct-port-I/O backend later for
   real non-blocking operation. (Standard BDOS AUX is blocking and has no poll,
   which a live terminal needs — hence the interface seam now.) The `1985`
   emulator exposes a serial PTY (`ext_serial`), which is a convenient test
   peer.
2. **VT100 parser + screen model.** A host-testable C state machine that
   consumes the serial byte stream and maintains a screen buffer.
3. **Renderer.** Translate the screen model to the local CP/M console.

## License

GPL-2.0 (matches the surrounding tooling). © 2026 Salvatore Bognanni.
