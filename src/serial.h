/*
 * serial.h - serial transport interface for vterm
 *
 * A thin, backend-agnostic seam between the terminal core and the serial
 * hardware. The current backend (serial.c) drives the Amstrad CPS8256
 * Z80-DART directly for true non-blocking polling; other backends (BDOS
 * AUX, other UARTs) could implement the same four calls.
 *
 * Baud rate and line parameters are assumed to be set up already by the
 * CP/M serial driver (SETSIO) / firmware; serial_init() does not touch them.
 */
#ifndef VTERM_SERIAL_H
#define VTERM_SERIAL_H

void          serial_init(void);
unsigned char serial_rx_ready(void);   /* nonzero if a byte is waiting     */
int           serial_getc(void);       /* 0..255, or -1 if nothing waiting */
void          serial_putc(unsigned char c);

#endif /* VTERM_SERIAL_H */
