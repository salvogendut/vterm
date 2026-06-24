# Makefile - vterm: CP/M VT100 terminal emulator (SDCC z80 -> .COM)
#
# Builds build/VTERM.COM from the sources in src/.  Uses the SDCC toolchain
# in ../sdcc (override with `make SDCC_BIN=/path/to/sdcc/bin`).

SDCC_BIN ?= ../sdcc/bin
SDCC     := $(SDCC_BIN)/sdcc
SDAS     := $(SDCC_BIN)/sdasz80
MAKEBIN  := $(SDCC_BIN)/makebin

# The PCW emulator and disk tooling used for headless testing. The emulator is
# an SDL3 app and the disk is built with cpmtools (pcw diskdef + libdsk); both
# live in the distrobox container, so the disk/test targets run inside it.
EMU        ?= ../1985-testing/1985
CONTAINER  ?= my-distrobox
BOOT_DISK  ?= $(HOME)/Documents/CPM Boot/CPM3 1-07.dsk

BUILD    := build
DIST     := dist
TARGET   := $(BUILD)/VTERM.COM
IHX      := $(BUILD)/vterm.ihx
DISK     := $(DIST)/vterm.dsk

# CP/M .COM layout: code at the start of the TPA (0x0100), data right after.
CFLAGS   := -mz80 --opt-code-size
LDFLAGS  := -mz80 --no-std-crt0 --code-loc 0x0100 --data-loc 0x0000

# crt0 must be linked first so that init lands at 0x0100.
OBJS := $(BUILD)/crt0.rel $(BUILD)/bdos.rel $(BUILD)/cps_io.rel \
        $(BUILD)/cpm.rel $(BUILD)/serial.rel $(BUILD)/vt100.rel \
        $(BUILD)/render.rel $(BUILD)/main.rel

.PHONY: all disk clean test-render test-serial test-vt100
all: $(TARGET)

# Host (gcc) unit tests for the portable VT100 engine, plus a Z80 cross-compile
# check so the engine keeps building for the target as well.
test-vt100: | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -O2 -o $(BUILD)/vt100_test \
	  src/vt100.c test/vt100_test.c
	$(BUILD)/vt100_test
	$(SDCC) $(CFLAGS) -c src/vt100.c -o $(BUILD)/vt100.rel
	@echo "vt100.c: host tests pass + Z80 cross-compile OK"

# Compile C sources.
$(BUILD)/%.rel: src/%.c | $(BUILD)
	$(SDCC) $(CFLAGS) -c $< -o $@

# Assemble .s sources (crt0, bdos) with the z80 assembler.  -g makes the
# SDCC-generated externals (l__DATA, s__INITIALIZER, ...) global; -o takes
# the object name literally, so pass the full <name>.rel.
$(BUILD)/%.rel: src/%.s | $(BUILD)
	$(SDAS) -g -o $@ $<

# Link to Intel HEX, then strip the 0x000-0x0FF padding to produce a .COM
# image that begins at 0x0100.
$(TARGET): $(OBJS)
	$(SDCC) $(LDFLAGS) $(OBJS) -o $(IHX)
	$(MAKEBIN) -p -o 256 $(IHX) $(TARGET)
	@echo "Built $(TARGET) ($$(wc -c < $(TARGET)) bytes)"

$(BUILD):
	mkdir -p $(BUILD)

# Build a bootable PCW CP/M Plus disk with VTERM.COM on it (copy of the boot
# disk + vterm.com via cpmtools). Boot it and run `vterm`. Runs in the
# container because cpmtools (pcw diskdef + libdsk) lives there.
disk: $(DISK)
$(DISK): $(TARGET)
	mkdir -p $(DIST)
	BOOT_DISK="$(BOOT_DISK)" COM="$(abspath $(TARGET))" OUT="$(abspath $(DISK))" \
	  distrobox enter $(CONTAINER) -- bash $(abspath test/make-pcw-disk.sh)

# Headless render test: boot the vterm disk, run vterm from A:, a VT100 peer
# sends a test pattern, and we screenshot the rendered result.
test-render: $(DISK)
	EMU="$(abspath $(EMU))" DISK="$(abspath $(DISK))" \
	  distrobox enter $(CONTAINER) -- bash $(abspath test/run-render.sh)
	@echo "Screenshot: $(BUILD)/vterm-render.ppm"

# Headless serial round-trip test: vterm + an echo peer on the serial PTY.
# Typed text only shows if it survives the full TX -> serial -> RX loop.
test-serial: $(DISK)
	EMU="$(abspath $(EMU))" DISK="$(abspath $(DISK))" \
	  distrobox enter $(CONTAINER) -- bash $(abspath test/run-serial.sh)
	@echo "Screenshot: $(BUILD)/vterm-serial.ppm"

clean:
	rm -rf $(BUILD) $(DIST)
