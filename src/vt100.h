/*
 * vt100.h - VT100/ANSI terminal engine for vterm
 *
 * A self-contained, I/O-free state machine: feed it the inbound byte stream
 * one byte at a time with vt_putc(), and it maintains an 80x24 screen model
 * (glyphs + per-cell attributes, cursor, scroll region, modes, tab stops). A
 * renderer walks the model to paint the local CP/M console; the host tests
 * assert it directly. Sequences that require a reply (DA, DSR) queue bytes
 * the caller drains with vt_resp_getc() and sends back to the host.
 *
 * Portable C: compiles with gcc (host unit tests) and SDCC (Z80 target).
 * No malloc, no library calls beyond <string.h>.
 */
#ifndef VTERM_VT100_H
#define VTERM_VT100_H

#define VT_COLS 80
#define VT_ROWS 24

/* Per-cell / current SGR attribute bits. */
#define VT_A_NORMAL    0x00
#define VT_A_BOLD      0x01
#define VT_A_UNDERLINE 0x02
#define VT_A_BLINK     0x04
#define VT_A_REVERSE   0x08

/* Parser states (exposed only so the struct is a complete type). */
#define VT_ST_GROUND 0
#define VT_ST_ESC    1
#define VT_ST_CSI    2
#define VT_ST_G0     3   /* after ESC ( : designate G0 charset */
#define VT_ST_G1     4   /* after ESC ) : designate G1 charset */

#define VT_NPARAMS 8
#define VT_RESP_SZ 16    /* outbound reply ring (DA/DSR) */

typedef struct {
    unsigned char ch[VT_ROWS][VT_COLS];   /* glyphs; ' ' = blank        */
    unsigned char attr[VT_ROWS][VT_COLS]; /* per-cell VT_A_* bits       */

    unsigned char row, col;               /* cursor, 0-based            */
    unsigned char cur_attr;               /* current SGR attribute      */
    unsigned char save_row, save_col, save_attr; /* DECSC / DECRC       */
    unsigned char save_gl, save_g0, save_g1;      /* DECSC saves charset */

    unsigned char top, bot;               /* scroll region, 0-based incl */
    unsigned char wrap_pending;           /* deferred last-column wrap   */
    unsigned char dirty;                  /* set when the model changes  */

    /* Modes. */
    unsigned char autowrap;               /* DECAWM (default on)        */
    unsigned char origin_mode;            /* DECOM  (default off)       */
    unsigned char cursor_visible;         /* DECTCEM (default on)       */

    /* Character sets: G0/G1 hold 0=ASCII or 1=DEC special graphics;
       gl selects which is active (locked-shift via SI/SO).             */
    unsigned char g0, g1, gl;

    unsigned char tabs[VT_COLS];          /* 1 = tab stop at column      */

    /* CSI parser scratch. */
    unsigned char state;
    unsigned int  params[VT_NPARAMS];
    unsigned char pidx;                   /* current param index         */
    unsigned char seen;                   /* any param char seen         */
    unsigned char priv;                   /* '?' private marker          */

    /* Outbound reply ring (engine -> host). */
    unsigned char resp[VT_RESP_SZ];
    unsigned char resp_head, resp_tail;
} VT;

void vt_init(VT *t);
void vt_putc(VT *t, unsigned char c);     /* feed one inbound byte       */
int  vt_resp_getc(VT *t);                 /* next reply byte, or -1      */

/* Convenience accessors (renderer / tests). */
unsigned char vt_char(const VT *t, unsigned char r, unsigned char c);
unsigned char vt_attr(const VT *t, unsigned char r, unsigned char c);
unsigned char vt_cursor_visible(const VT *t);

#endif /* VTERM_VT100_H */
