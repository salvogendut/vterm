# Makefile - vterm: CP/M VT100 terminal emulator (SDCC z80 -> .COM)
#
# Builds build/VTERM.COM from the sources in src/.  Uses the SDCC toolchain
# in ../sdcc (override with `make SDCC_BIN=/path/to/sdcc/bin`).

SDCC_BIN ?= ../sdcc/bin
SDCC     := $(SDCC_BIN)/sdcc
SDAS     := $(SDCC_BIN)/sdasz80
MAKEBIN  := $(SDCC_BIN)/makebin

# CP/M disk image tooling and the PCW emulator used for headless testing.
IDSK     ?= ../idsk/iDSK
EMU_PCW  ?= ../1985/1985

BUILD    := build
DIST     := dist
TARGET   := $(BUILD)/VTERM.COM
IHX      := $(BUILD)/vterm.ihx
DISK     := $(DIST)/vterm.dsk

# CP/M .COM layout: code at the start of the TPA (0x0100), data right after.
CFLAGS   := -mz80 --opt-code-size
LDFLAGS  := -mz80 --no-std-crt0 --code-loc 0x0100 --data-loc 0x0000

# crt0 must be linked first so that init lands at 0x0100.
OBJS := $(BUILD)/crt0.rel $(BUILD)/bdos.rel $(BUILD)/cpm.rel $(BUILD)/main.rel

.PHONY: all disk clean test-pcw
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

# Headless smoke test on the PCW emulator: boot CP/M Plus from drive A
# (taken from ~/.config/1985/1985.conf), mount the vterm disk as B:, type
# `b:`/`vterm`, and capture a screenshot to build/vterm-pcw.ppm.  Leading
# newlines + ONE_K_PASTE_GAP delay the keystrokes until after the boot.
test-pcw: $(DISK)
	ONE_K_PASTE_GAP=60 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $(EMU_PCW) \
	  --disk-b $(abspath $(DISK)) \
	  --paste "$$(printf '\n\n\n\n\n\n\n\nb:\nvterm\n')" \
	  --screenshot-at 1600:$(abspath $(BUILD))/vterm-pcw.ppm --exit-after 1650
	@echo "Screenshot: $(BUILD)/vterm-pcw.ppm"

clean:
	rm -rf $(BUILD) $(DIST)
