# Makefile - vterm: CP/M VT100 terminal emulator (SDCC z80 -> .COM)
#
# Builds build/VTERM.COM from the sources in src/.  Uses the SDCC toolchain
# in ../sdcc (override with `make SDCC_BIN=/path/to/sdcc/bin`).

SDCC_BIN ?= ../sdcc/bin
SDCC     := $(SDCC_BIN)/sdcc
SDAS     := $(SDCC_BIN)/sdasz80
MAKEBIN  := $(SDCC_BIN)/makebin

# CP/M disk image tooling and the PCW emulator used for headless testing.
# The emulator is an SDL3 app built against the distrobox container's SDL3,
# so the test targets run it inside that container.
IDSK      ?= ../idsk/iDSK
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
        $(BUILD)/cpm.rel $(BUILD)/serial.rel $(BUILD)/main.rel

.PHONY: all disk clean test-pcw test-serial
all: $(TARGET)

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

# Package VTERM.COM onto a CP/M-readable .dsk (fresh CPC DATA-format disk,
# raw mode -t 2 because a .COM has no AMSDOS header).  This image is read
# as a data drive by both 1984 (CPC) and 1985 (PCW).
disk: $(DISK)
$(DISK): $(TARGET)
	mkdir -p $(DIST)
	rm -f $(DISK)
	$(IDSK) $(DISK) -n
	$(IDSK) $(DISK) -i $(TARGET) -t 2 -f
	$(IDSK) $(DISK) -l

# Headless console smoke test on the PCW emulator (inside the container):
# boot CP/M Plus, mount the vterm disk as B:, type `b:`/`vterm`, and capture
# build/vterm-pcw.ppm.  Leading newlines + ONE_K_PASTE_GAP delay the
# keystrokes until after boot (see docs/BUILD.md).
test-pcw: $(DISK)
	distrobox enter $(CONTAINER) -- bash -c 'ONE_K_PASTE_GAP=60 \
	  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$(abspath $(EMU))" \
	  --disk-a "$(BOOT_DISK)" --disk-b "$(abspath $(DISK))" \
	  --paste "$$(printf "\n\n\n\n\n\n\n\nb:\nvterm\n")" \
	  --screenshot-at 1600:"$(abspath $(BUILD))/vterm-pcw.ppm" --exit-after 1650'
	@echo "Screenshot: $(BUILD)/vterm-pcw.ppm"

# Headless serial round-trip test: vterm passthrough + an echo peer on the
# serial PTY. Typed text only shows if it survives the full TX->RX loop.
test-serial: $(DISK)
	EMU="$(abspath $(EMU))" BOOT_DISK="$(BOOT_DISK)" DISK="$(abspath $(DISK))" \
	  distrobox enter $(CONTAINER) -- bash $(abspath test/run-serial.sh)
	@echo "Screenshot: $(BUILD)/vterm-serial.ppm"

clean:
	rm -rf $(BUILD) $(DIST)
