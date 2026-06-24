# vterm

A VT100 terminal emulator for **CP/M**, written in C and compiled to a `.COM`
with the SDCC Z80 toolchain. It connects to a remote host over a **serial
(RS-232)** line, renders incoming VT100 escape sequences onto the **CP/M
console (via BDOS)**, and sends local keystrokes back out the serial line.

Runs on the **Amstrad PCW** (CPS8256 Z80-DART serial) and the **Amstrad CPC**
(USIfAC II serial); both use Amstrad CP/M Plus, whose VT52 console is shared.
Select the target with `make PLATFORM=pcw` (default) or `make PLATFORM=cpc`.

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
  screen model. Covered by host unit tests (`make test-vt100`) and
  cross-compiles for Z80.
- **Milestone 4 — renderer + full terminal: complete.** `render.[ch]` diffs
  the screen model against a shadow buffer and paints changed cells onto the
  PCW's VT52 console (`ESC Y` positioning, `ESC p/q` inverse, `ESC r/u`
  bright, `ESC e/f` cursor). `main.c` is the full terminal loop (serial →
  VT100 → render; keyboard → serial; replies → serial).
- **Milestone 5 — wider VT100/ANSI coverage: complete.** Line-drawing charset,
  insert/delete line & char, scroll/erase ops, tab stops, DEC modes
  (autowrap/origin/cursor), 256-colour/truecolour SGR skipping, and DA/DSR
  host replies. Verified on the PCW with a TUI test pattern.
- **Milestone 6 — telnet: complete.** A telnet IAC filter (`telnet.[ch]`)
  strips negotiation bytes before the VT100 engine and replies with the right
  WILL/WONT/DO/DONT, so real telnet hosts render cleanly. **Confirmed live:**
  dialing `telehack.com` through the PerryFi modem renders the full host
  session (menus, `cal` with today inverse-highlighted).
- **Milestone 7 — alternate screen: complete.** DEC modes `?1049`/`?47`/`?1047`
  (+ `?1048` cursor save) keep a second screen buffer, so full-screen apps
  (vi, less, tmux) restore the prior screen on exit.
- **Milestone 8 — Amstrad CPC target: complete.** `make PLATFORM=cpc` builds a
  CPC `.COM`. The VT100 engine, telnet filter, CP/M layer and (because both
  Amstrad machines share the CP/M Plus VT52 console) the renderer are all
  reused; only the serial backend swaps to the USIfAC II (`&FBD0`/`&FBD1`).
  Verified on the 1984 emulator, including dialling a host through PerryFi.

This is a working VT100 telnet terminal on two machines. Remaining work is
breadth and UX. See [Roadmap](#roadmap).

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
| `src/serial.h` | Serial transport interface (`serial_getc`, `serial_putc`, …) |
| `src/serial_cps.c` / `src/cps_io.s` | PCW backend: CPS8256 Z80-DART (`0xE0`/`0xE1`) |
| `src/serial_usifac.c` / `src/usifac_io.s` | CPC backend: USIfAC II (`&FBD0`/`&FBD1`) |
| `src/telnet.h` / `src/telnet.c` | Telnet IAC filter on the inbound stream (strips negotiation, replies WILL/WONT/DO/DONT) |
| `src/vt100.h` / `src/vt100.c` | Portable VT100/ANSI engine: `vt_putc` drives an 80×24 screen model |
| `src/render.h` / `src/render_vt52.c` | Diff the screen model onto the Amstrad CP/M Plus VT52 console (`ESC Y`, inverse, scroll) — shared by PCW and CPC |
| `src/main.c` | Terminal loop: serial → VT100 → render; keyboard → serial |
| `test/` | VT100 host unit tests, PCW disk builder, and headless emulator tests |

## Roadmap

1. ~~**Serial transport.**~~ Done — `serial.h` seam with a direct CPS8256
   Z80-DART backend (non-blocking poll of ports `0xE0`/`0xE1`); baud is left to
   `SETSIO`/firmware. A BDOS-AUX backend could be added behind the same
   interface for portability.
2. ~~**VT100 parser + screen model.**~~ Done — `vt100.[ch]`, a host-testable
   state machine maintaining an 80×24 screen model.
3. ~~**Renderer + terminal loop.**~~ Done — `render.[ch]` diffs the model onto
   the PCW VT52 console; `main.c` ties serial, VT100 and render together.
4. ~~**Wider VT100/ANSI coverage.**~~ Done — line-drawing charset (`ESC(0`),
   IL/DL/ICH/DCH/ECH editing, CNL/CPL/SU/SD, tab stops (HTS/TBC/CHT/CBT),
   modes (DECAWM/DECOM/DECTCEM), 256-colour/truecolour SGR skipping, and DA/DSR
   replies (queued for the host). Covered by 52 host checks.
5. ~~**Telnet.**~~ Done — `telnet.[ch]` IAC filter; verified live against
   `telehack.com` via the PerryFi modem.
6. ~~**Alternate screen.**~~ Done — `?1049`/`?47`/`?1047` second screen buffer.
7. **Further breadth.** A configurable console back-end for other CP/M machines
   (the renderer is PCW-VT52-specific today), `SETSIO`-free DART setup for real
   hardware, and UX (status line, capture, file transfer).

## License

GPL-2.0 (matches the surrounding tooling). © 2026 Salvatore Bognanni.
