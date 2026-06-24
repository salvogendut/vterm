/*
 * cpm.c - CP/M BDOS console helpers for vterm
 */
#include "cpm.h"

void conout(char c)
{
	bdos(BDOS_C_WRITE, (unsigned char)c);
}

/*
 * Read one console character with NO echo, using direct console I/O
 * (BDOS function 6, E=0xFF), polling until a key is available.  We avoid
 * function 1 (C_READ) because it echoes and acts on control characters --
 * a terminal must control the display itself and pass raw bytes through.
 */
/*
 * Non-blocking raw console read (BDOS function 6, E=0xFF): returns the
 * character, or 0 if no key is waiting. No echo, no control-char handling.
 */
unsigned char conkey(void)
{
	return bdos(BDOS_C_RAWIO, 0x00FF);
}

char conin(void)
{
	unsigned char c;
	do {
		c = conkey();
	} while (c == 0);
	return (char)c;
}

unsigned char constat(void)
{
	return bdos(BDOS_C_STAT, 0);
}

/*
 * Write a NUL-terminated string a character at a time.  We avoid BDOS
 * function 9 (print string) because it terminates on '$', which is a
 * legitimate byte in terminal traffic, and because per-char output keeps
 * CR/LF handling under our control.
 */
void prints(const char *s)
{
	while (*s)
		conout(*s++);
}
