/*
 * main.c - vterm
 *
 * The terminal loop. Inbound serial bytes feed the VT100 engine, which
 * maintains the screen model; the renderer paints changed cells onto the
 * CP/M console. Local keystrokes go straight out the serial line.
 *
 *   serial -> vt_putc(model) -> render -> CP/M console
 *   keyboard -> serial
 *
 * Press Ctrl-] to quit back to CP/M.
 */
#include "cpm.h"
#include "serial.h"
#include "telnet.h"
#include "vt100.h"
#include "render.h"

#define CTRL_RBRACKET  0x1D   /* Ctrl-] : local "quit" key */
#define BURST_CAP      1024   /* max inbound bytes per loop before repainting */

static VT     screen;         /* the terminal screen model (~3.8 KB, BSS) */
static Telnet tn;             /* telnet IAC filter on the inbound stream  */

void main(void)
{
	int           ch;
	unsigned char k;
	unsigned int  n;

	serial_init();
	telnet_init(&tn);
	vt_init(&screen);
	render_init();        /* clear console, cursor home */

	/* Startup banner drawn THROUGH the VT engine, so a blank screen vs a
	 * visible banner tells us whether render works independent of serial. */
	{
		const char *b = "vterm ready -- waiting for host\r\n";
		while (*b)
			vt_putc(&screen, (unsigned char)*b++);
		render_flush(&screen);
		screen.dirty = 0;
	}

	for (;;) {
		/* Pull a burst of inbound bytes into the model, then repaint
		 * once -- batching keeps console output and keys responsive. */
		n = 0;
		while ((ch = serial_getc()) >= 0) {
			int d = telnet_filter(&tn, (unsigned char)ch);
			if (d >= 0)
				vt_putc(&screen, (unsigned char)d);
			if (++n >= BURST_CAP)
				break;
		}
		if (screen.dirty) {
			render_flush(&screen);
			screen.dirty = 0;
		}

		/* Send queued replies back to the host: telnet negotiation first,
		 * then any VT100 report (DA / DSR). */
		while ((ch = telnet_resp_getc(&tn)) >= 0)
			serial_putc((unsigned char)ch);
		while ((ch = vt_resp_getc(&screen)) >= 0)
			serial_putc((unsigned char)ch);

		/* Forward one local keystroke to the line. */
		k = conkey();
		if (k) {
			if (k == CTRL_RBRACKET)
				break;
			serial_putc(k);
		}
	}

	render_init();        /* leave a clean console */
	prints("vterm: bye\r\n");
}
