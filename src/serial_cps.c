/*
 * serial.c - CPS8256 (Z80-DART) serial backend for vterm
 *
 * Channel A of the DART carries the serial line. Status comes from RR0
 * (read via the asm helpers in cps_io.s); data goes through port 0xE0.
 */
#include "serial.h"

/* Port glue (cps_io.s). */
extern unsigned char cps_status(void);   /* RR0 */
extern unsigned char cps_read(void);     /* data port, RX */
extern void          cps_write(unsigned char c); /* data port, TX */

/* RR0 status bits. */
#define RR0_RX_AVAIL   0x01
#define RR0_TX_EMPTY   0x04

void serial_init(void)
{
	/* Nothing to configure: SETSIO / firmware owns baud and line setup,
	 * and the CPS8256 passes data without us toggling the enable bits. */
}

unsigned char serial_rx_ready(void)
{
	return (cps_status() & RR0_RX_AVAIL) ? 1 : 0;
}

int serial_getc(void)
{
	if (!(cps_status() & RR0_RX_AVAIL))
		return -1;
	return (int)(unsigned char)cps_read();
}

void serial_putc(unsigned char c)
{
	while (!(cps_status() & RR0_TX_EMPTY))
		;
	cps_write(c);
}
