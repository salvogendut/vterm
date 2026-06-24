;--------------------------------------------------------------------------
;  usifac_io.s - Amstrad CPC USIfAC II RS232 port access
;
;  The USIfAC II serial interface sits on the CPC I/O space (gated on the
;  MX4 expansion bus):
;     &FBD0  r/w  DATA    (read = pop RX byte, write = push TX byte)
;     &FBD1  r    STATUS  (0xFF when a byte is waiting, 0x01 when empty)
;
;  The CPC decodes the full 16-bit I/O address, so these use the BC form
;  of IN/OUT (B = 0xFB, C = 0xD0/0xD1) rather than immediate ports.
;  With the PerryFi modem plugged in, the data path is the modem.
;--------------------------------------------------------------------------
	.module usifac_io
	.globl	_usifac_status
	.globl	_usifac_read
	.globl	_usifac_write

	.area	_CODE

;; unsigned char usifac_status(void)   -> read &FBD1 (bit 7 set = RX ready)
_usifac_status::
	ld	bc, #0xFBD1
	in	a, (c)
	ret

;; unsigned char usifac_read(void)     -> read one byte from &FBD0
_usifac_read::
	ld	bc, #0xFBD0
	in	a, (c)
	ret

;; void usifac_write(unsigned char c)  -> c in A (sdcccall(1)); write &FBD0
_usifac_write::
	ld	b, #0xFB	;; A (the data) is untouched by these loads
	ld	c, #0xD0
	out	(c), a
	ret
