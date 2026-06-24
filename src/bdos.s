;--------------------------------------------------------------------------
;  bdos.s - CP/M BDOS call gateway for SDCC z80 (sdcccall(1))
;
;  SDCC's default calling convention (sdcccall(1)) passes the first
;  argument (unsigned char func) in A and the second (unsigned int arg)
;  in DE.  CP/M's BDOS expects the function number in C and the parameter
;  in DE, entered via "call 5", and returns its result in A (8-bit) and
;  HL (16-bit, with A == L).  So the gateway only has to move A -> C.
;--------------------------------------------------------------------------
	.module bdos
	.globl	_bdos
	.globl	_bdos16

	.area	_CODE

;; unsigned char bdos(unsigned char func /*A*/, unsigned int arg /*DE*/)
;;   returns the 8-bit BDOS result in A (sdcccall(1) char return register)
_bdos::
	ld	c, a
	call	5
	ret

;; unsigned int bdos16(unsigned char func /*A*/, unsigned int arg /*DE*/)
;;   returns the 16-bit BDOS result in HL (sdcccall(1) int return register)
_bdos16::
	ld	c, a
	call	5
	ret
