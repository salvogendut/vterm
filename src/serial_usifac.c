/*
 * serial_usifac.c - USIfAC II (Amstrad CPC) serial backend for vterm
 *
 * Implements the serial.h interface over the USIfAC II RS232 board (ports
 * &FBD0 data / &FBD1 status, via usifac_io.s). With the PerryFi modem
 * plugged in, this is the path to ATDT-dialled TCP hosts. Baud and line
 * setup are left to the firmware / SETSIO, as on the PCW.
 */
#include "serial.h"

extern unsigned char usifac_status(void);
extern unsigned char usifac_read(void);
extern void          usifac_write(unsigned char c);

/* STATUS is 0xFF when a byte is waiting, 0x01 when empty: test bit 7. */
#define RX_READY 0x80

void serial_init(void)
{
}

unsigned char serial_rx_ready(void)
{
	return (usifac_status() & RX_READY) ? 1 : 0;
}

int serial_getc(void)
{
	if (!(usifac_status() & RX_READY))
		return -1;
	return (int)(unsigned char)usifac_read();
}

void serial_putc(unsigned char c)
{
	usifac_write(c);
}
