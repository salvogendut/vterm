/*
 * main.c - vterm scaffold entry point
 *
 * This is the toolchain/scaffolding milestone: it proves the SDCC -> .COM
 * build chain, the crt0 startup, and bidirectional console I/O through the
 * BDOS layer.  It does NOT yet do any serial or VT100 work -- those are the
 * next milestones (see README.md).
 *
 * Behaviour: print a banner, then echo typed characters until the user
 * presses Return, then exit back to CP/M.
 */
#include "cpm.h"

#define CR  '\r'
#define LF  '\n'

void main(void)
{
	char c;

	prints("vterm - CP/M VT100 terminal (scaffold build)\r\n");
	prints("Type a line and press Return to exit.\r\n> ");

	for (;;) {
		c = conin();
		if (c == CR) {
			conout(CR);
			conout(LF);
			break;
		}
		conout(c);
	}

	prints("bye\r\n");
}
