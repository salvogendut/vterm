/*
 * main.c - vterm
 *
 * Milestone 2: serial passthrough. A bare terminal loop that pumps bytes
 * between the CP/M console and the serial line:
 *
 *   serial -> console   (display whatever the remote sends)
 *   console -> serial   (send local keystrokes to the remote)
 *
 * No VT100 interpretation yet -- raw bytes both ways. The VT100 parser and
 * screen model are the next milestone; they will sit on the serial->console
 * path. Press Ctrl-] to quit back to CP/M.
 */
#include "cpm.h"
#include "serial.h"

#define CTRL_RBRACKET  0x1D   /* Ctrl-]  : local "quit" key */

void main(void)
{
	int           ch;
	unsigned char k;

	prints("vterm: serial passthrough -- Ctrl-] to quit\r\n");
	serial_init();

	for (;;) {
		/* Drain one inbound byte (non-blocking) to the console. */
		ch = serial_getc();
		if (ch >= 0)
			conout((char)ch);

		/* Forward one local keystroke (non-blocking) to the line. */
		k = conkey();
		if (k) {
			if (k == CTRL_RBRACKET)
				break;
			serial_putc(k);
		}
	}

	prints("\r\nvterm: bye\r\n");
}
