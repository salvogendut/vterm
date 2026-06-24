;--------------------------------------------------------------------------
;  crt0.s - C runtime startup for a CP/M .COM program (SDCC z80)
;
;  Adapted from SDCC's generic z80 crt0 (Copyright (C) 2000 Michael Hope,
;  GPL with the SDCC linking exception).  Retargeted for CP/M:
;    * program is ORG'd at 0x0100 (start of the TPA)
;    * the stack is placed at the top of the TPA (pointer at 0x0006)
;    * returning from main() warm-boots CP/M (jp 0x0000)
;--------------------------------------------------------------------------
	.module crt0
	.globl	_main

	;; No ABS header: the image is anchored purely by --code-loc 0x0100.
	;; crt0 is linked first, so the _CODE area below starts at 0x0100 and
	;; "init" is the first instruction CP/M executes when it jumps there.
	.area	_CODE
init:
	;; Put the stack at the top of the TPA.  Address 0x0006 holds the
	;; entry address of the BDOS (== top of usable memory); use it as SP.
	ld	hl, (0x0006)
	ld	sp, hl

	;; Initialise global variables, then run the program.
	call	gsinit
	call	_main

	;; main() returned -> warm-boot CP/M.
	jp	_exit

	;; Ordering of segments for the linker.
	.area	_HOME
	.area	_CODE
	.area	_INITIALIZER
	.area	_GSINIT
	.area	_GSFINAL

	.area	_DATA
	.area	_INITIALIZED
	.area	_BSEG
	.area	_BSS
	.area	_HEAP

	.area	_CODE
_exit::
	;; Return control to CP/M (warm boot via the BIOS jump at 0x0000).
	jp	0x0000

	.area	_GSINIT
gsinit::
	;; Zero default-initialised global variables (_DATA / _BSS).
	ld	bc, #l__DATA
	ld	a, b
	or	a, c
	jr	Z, zeroed_data
	ld	hl, #s__DATA
	ld	(hl), #0x00
	dec	bc
	ld	a, b
	or	a, c
	jr	Z, zeroed_data
	ld	e, l
	ld	d, h
	inc	de
	ldir
zeroed_data:

	;; Copy explicitly-initialised global variables into place.
	ld	bc, #l__INITIALIZER
	ld	a, b
	or	a, c
	jr	Z, gsinit_next
	ld	de, #s__INITIALIZED
	ld	hl, #s__INITIALIZER
	ldir
gsinit_next:

	.area	_GSFINAL
	ret
