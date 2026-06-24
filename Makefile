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

# Amstrad CPC (PLATFORM=cpc) testing: the 1984 emulator, iDSK (handles CPC DSK
# format natively), the CPC ROMs, and a CP/M Plus boot disk.
IDSK          ?= ../idsk/iDSK
EMU_CPC       ?= ../1984/1984
CPC_ROMS      ?= $(HOME)/.config/1984/roms
CPC_BOOT_DISK ?= $(HOME)/DIMITRI/Attachments-Re_ Amstrad CPC emulator/cpmplue1.dsk
CPC_DISK       = $(DIST)/vterm-cpc.dsk

BUILD    := build
DIST     := dist
TARGET   := $(BUILD)/VTERM.COM
IHX      := $(BUILD)/vterm.ihx
DISK     := $(DIST)/vterm.dsk

# CP/M .COM layout: code at the start of the TPA (0x0100), data right after.
CFLAGS   := -mz80 --opt-code-size
LDFLAGS  := -mz80 --no-std-crt0 --code-loc 0x0100 --data-loc 0x0000

# Target platform: pcw (Amstrad PCW, default) or cpc (Amstrad CPC). Both run
# Amstrad CP/M Plus, whose console is the same VT52, so the renderer
# (render_vt52) is shared -- only the serial backend differs (the PCW's CPS8256
# Z80-DART vs the CPC's USIfAC II).
PLATFORM ?= pcw

ifeq ($(PLATFORM),cpc)
PLAT_IO     := $(BUILD)/usifac_io.rel
PLAT_SERIAL := $(BUILD)/serial_usifac.rel
else
PLAT_IO     := $(BUILD)/cps_io.rel
PLAT_SERIAL := $(BUILD)/serial_cps.rel
endif

# crt0 must be linked first so that init lands at 0x0100.
OBJS := $(BUILD)/crt0.rel $(BUILD)/bdos.rel $(PLAT_IO) \
        $(BUILD)/cpm.rel $(PLAT_SERIAL) $(BUILD)/telnet.rel \
        $(BUILD)/vt100.rel $(BUILD)/render_vt52.rel $(BUILD)/main.rel

.PHONY: all disk clean run test-render test-serial test-vt100
all: $(TARGET)

# Interactive session: launch the emulator GUI with the serial line bridged to
# a real shell. Boot CP/M, type `vterm`, and use the shell (Ctrl-] quits vterm).
run: $(DISK)
	EMU="$(abspath $(EMU))" DISK="$(abspath $(DISK))" \
	  distrobox enter $(CONTAINER) -- bash $(abspath test/run-interactive.sh)

# Host (gcc) unit tests for the portable VT100 engine and telnet filter, plus a
# Z80 cross-compile check so they keep building for the target as well.
test-vt100: | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -O2 -o $(BUILD)/vt100_test \
	  src/vt100.c src/telnet.c test/vt100_test.c
	$(BUILD)/vt100_test
	$(SDCC) $(CFLAGS) -c src/vt100.c -o $(BUILD)/vt100.rel
	$(SDCC) $(CFLAGS) -c src/telnet.c -o $(BUILD)/telnet.rel
	@echo "vt100.c + telnet.c: host tests pass + Z80 cross-compile OK"

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

ifeq ($(PLATFORM),cpc)

# Build a bootable CPC CP/M Plus disk (copy the boot disk + add VTERM.COM with
# iDSK, which handles CPC DSK format natively). Boot it with `|cpm`, run `vterm`.
disk: $(TARGET)
	mkdir -p $(DIST)
	CPC_BOOT_DISK="$(CPC_BOOT_DISK)" IDSK="$(abspath $(IDSK))" \
	  COM="$(abspath $(TARGET))" OUT="$(abspath $(CPC_DISK))" \
	  bash $(abspath test/make-cpc-disk.sh)

# Headless CPC render test on the 1984 emulator (boots CP/M Plus, runs vterm,
# a VT100 peer draws over the USIfAC serial).
test-render: disk
	EMU="$(abspath $(EMU_CPC))" DISK="$(abspath $(CPC_DISK))" ROMS="$(CPC_ROMS)" \
	  distrobox enter $(CONTAINER) -- bash $(abspath test/run-cpc.sh)
	@echo "Screenshot: $(BUILD)/vterm-cpc.ppm"

else

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

endif

# Headless serial round-trip test: vterm + an echo peer on the serial PTY.
# Typed text only shows if it survives the full TX -> serial -> RX loop.
test-serial: $(DISK)
	EMU="$(abspath $(EMU))" DISK="$(abspath $(DISK))" \
	  distrobox enter $(CONTAINER) -- bash $(abspath test/run-serial.sh)
	@echo "Screenshot: $(BUILD)/vterm-serial.ppm"

clean:
	rm -rf $(BUILD) $(DIST)
