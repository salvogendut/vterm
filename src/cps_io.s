;--------------------------------------------------------------------------
;  cps_io.s - Amstrad CPS8256 Z80-DART port access (serial channel A)
;
;  The CPS8256 serial/parallel add-on exposes a Z80-DART on channel A:
;     0xE0  data    (read = RX byte, write = TX byte)
;     0xE1  control (write WR0 to select a register; read returns RRx)
;
;  Only fixed ports are touched, so the helpers use immediate-port I/O
;  (single argument in A under sdcccall(1)) and need no register juggling.
;  The PCW decodes only the low 8 bits of the I/O address, so the high
;  byte placed on the bus by `in a,(n)` / `out (n),a` is irrelevant.
;--------------------------------------------------------------------------
	.module cps_io
	.globl	_cps_status
	.globl	_cps_read
	.globl	_cps_write

	.area	_CODE

;; unsigned char cps_status(void)
;;   Point the DART register pointer at RR0 (WR0 = 0) then read RR0.
;;   RR0 bit 0 = RX char available, bit 2 = TX buffer empty.
_cps_status::
	xor	a		; A = 0  -> WR0 = 0 selects RR0
	out	(0xE1), a
	in	a, (0xE1)	; read RR0
	ret

;; unsigned char cps_read(void)
;;   Read one data byte from channel A.  Only valid when RR0 RX bit is set;
;;   reading otherwise returns 0 (the DART has nothing latched).
_cps_read::
	in	a, (0xE0)
	ret

;; void cps_write(unsigned char c)
;;   c arrives in A (sdcccall(1) first byte argument); send it on channel A.
_cps_write::
	out	(0xE0), a
	ret
