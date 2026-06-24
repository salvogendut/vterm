/*
 * cpm.h - thin CP/M BDOS console layer for vterm
 *
 * Wraps the asm BDOS gateway (bdos.s) with the handful of console
 * primitives the terminal needs.  Serial transport and the VT100 engine
 * live elsewhere; this file is just the OS interface.
 */
#ifndef VTERM_CPM_H
#define VTERM_CPM_H

/* --- BDOS function numbers (the ones we use) ------------------------- */
#define BDOS_C_READ     1   /* console input  (blocking, echoes)        */
#define BDOS_C_WRITE    2   /* console output (one char in E)           */
#define BDOS_AUX_IN     3   /* AUX/reader input  (serial, blocking)     */
#define BDOS_AUX_OUT    4   /* AUX/punch output  (serial)               */
#define BDOS_C_RAWIO    6   /* direct console I/O (no echo, non-block)  */
#define BDOS_PRINT_STR  9   /* print '$'-terminated string              */
#define BDOS_C_STAT     11  /* console status: nonzero if a key is ready*/
#define BDOS_S_BDOSVER  12  /* return CP/M / BDOS version in HL         */

/* --- BDOS gateways (implemented in bdos.s) -------------------------- */
unsigned char bdos(unsigned char func, unsigned int arg);
unsigned int  bdos16(unsigned char func, unsigned int arg);

/* --- console helpers (implemented in cpm.c) ------------------------- */
void          conout(char c);        /* write one char to the console   */
char          conin(void);           /* read one char (blocks, no echo) */
unsigned char conkey(void);          /* raw read, 0 if no key (no echo) */
unsigned char constat(void);         /* nonzero if a key is waiting      */
void          prints(const char *s); /* write a NUL-terminated string    */

#endif /* VTERM_CPM_H */
