/*
 * vt100.c - VT100/ANSI terminal engine. See vt100.h.
 *
 * Implements a practical VT100 subset: printable text with auto-wrap (with
 * the real last-column deferred-wrap quirk), the C0 controls a host actually
 * sends (BS, TAB, LF, CR, BEL), the ESC singles (IND, RI, NEL, DECSC/DECRC,
 * RIS), and the common CSI sequences (cursor moves, CUP, ED, EL, SGR,
 * DECSTBM, plus ANSI save/restore). Enough to drive ls/vi/ANSI BBS output.
 */
#include "vt100.h"
#include <string.h>

#define TABSTOP 8

/* ---- low-level screen ops ------------------------------------------- */

static void erase_cells(VT *t, unsigned char r,
                        unsigned char c0, unsigned char c1)
{
    unsigned char c;
    for (c = c0; c <= c1; c++) {
        t->ch[r][c]   = ' ';
        t->attr[r][c] = VT_A_NORMAL;
    }
}

static void scroll_up(VT *t, unsigned char n)
{
    unsigned char r;
    if (n == 0) return;
    for (r = t->top; (unsigned char)(r + n) <= t->bot; r++) {
        memcpy(t->ch[r],   t->ch[r + n],   VT_COLS);
        memcpy(t->attr[r], t->attr[r + n], VT_COLS);
    }
    for (r = (unsigned char)(t->bot - n + 1); r <= t->bot; r++)
        erase_cells(t, r, 0, VT_COLS - 1);
    t->dirty = 1;
}

static void scroll_down(VT *t, unsigned char n)
{
    unsigned char r;
    if (n == 0) return;
    for (r = t->bot; (unsigned char)(r) >= (unsigned char)(t->top + n); r--) {
        memcpy(t->ch[r],   t->ch[r - n],   VT_COLS);
        memcpy(t->attr[r], t->attr[r - n], VT_COLS);
        if (r == 0) break;            /* guard underflow on unsigned r */
    }
    for (r = t->top; r <= (unsigned char)(t->top + n - 1); r++)
        erase_cells(t, r, 0, VT_COLS - 1);
    t->dirty = 1;
}

/* Move down one line, scrolling the region if at the bottom margin (IND/LF). */
static void index_down(VT *t)
{
    if (t->row == t->bot)
        scroll_up(t, 1);
    else if (t->row < VT_ROWS - 1)
        t->row++;
}

/* Move up one line, scrolling the region if at the top margin (RI). */
static void index_up(VT *t)
{
    if (t->row == t->top)
        scroll_down(t, 1);
    else if (t->row > 0)
        t->row--;
}

static void put_glyph(VT *t, unsigned char c)
{
    if (t->wrap_pending) {            /* deferred wrap from last column */
        t->col = 0;
        index_down(t);
        t->wrap_pending = 0;
    }
    t->ch[t->row][t->col]   = c;
    t->attr[t->row][t->col] = t->cur_attr;
    if (t->col + 1 >= VT_COLS)
        t->wrap_pending = 1;          /* sit on last column until next char */
    else
        t->col++;
    t->dirty = 1;
}

/* ---- cursor helpers ------------------------------------------------- */

static void clamp_cursor(VT *t)
{
    if (t->row >= VT_ROWS) t->row = VT_ROWS - 1;
    if (t->col >= VT_COLS) t->col = VT_COLS - 1;
    t->wrap_pending = 0;
}

/* ---- C0 control bytes ----------------------------------------------- */

static void ctrl(VT *t, unsigned char c)
{
    switch (c) {
    case 0x08:                        /* BS  */
        if (t->col > 0) t->col--;
        t->wrap_pending = 0;
        break;
    case 0x09:                        /* HT  */
        t->col = (unsigned char)((t->col / TABSTOP + 1) * TABSTOP);
        if (t->col >= VT_COLS) t->col = VT_COLS - 1;
        t->wrap_pending = 0;
        break;
    case 0x0A:                        /* LF  */
    case 0x0B:                        /* VT  -> treat as LF */
    case 0x0C:                        /* FF  -> treat as LF */
        index_down(t);
        t->wrap_pending = 0;
        break;
    case 0x0D:                        /* CR  */
        t->col = 0;
        t->wrap_pending = 0;
        break;
    default:                          /* BEL and others: no display effect */
        break;
    }
}

/* ---- CSI dispatch --------------------------------------------------- */

/* param i with a default substituted when it is absent or zero. */
static unsigned int p_or(const VT *t, unsigned char i, unsigned int def)
{
    unsigned int v = (i < VT_NPARAMS) ? t->params[i] : 0;
    return v ? v : def;
}

static void csi_dispatch(VT *t, unsigned char f)
{
    unsigned int a = p_or(t, 0, 1);
    unsigned int b;
    unsigned char i, r, c0, c1;

    switch (f) {
    case 'A':                          /* CUU */
        t->row = (t->row > a) ? (unsigned char)(t->row - a) : 0;
        t->wrap_pending = 0;
        break;
    case 'B':                          /* CUD */
        t->row = (unsigned char)(t->row + a);
        clamp_cursor(t);
        break;
    case 'C':                          /* CUF */
        t->col = (unsigned char)(t->col + a);
        clamp_cursor(t);
        break;
    case 'D':                          /* CUB */
        t->col = (t->col > a) ? (unsigned char)(t->col - a) : 0;
        t->wrap_pending = 0;
        break;
    case 'G':                          /* CHA - column */
        t->col = (unsigned char)(a - 1);
        clamp_cursor(t);
        break;
    case 'd':                          /* VPA - row */
        t->row = (unsigned char)(a - 1);
        clamp_cursor(t);
        break;
    case 'H':                          /* CUP */
    case 'f':                          /* HVP */
        b = p_or(t, 1, 1);
        t->row = (unsigned char)(a - 1);
        t->col = (unsigned char)(b - 1);
        clamp_cursor(t);
        break;
    case 'J':                          /* ED */
        b = t->params[0];              /* raw param, default 0 */
        if (b == 0) {                  /* cursor..end */
            erase_cells(t, t->row, t->col, VT_COLS - 1);
            for (r = (unsigned char)(t->row + 1); r < VT_ROWS; r++)
                erase_cells(t, r, 0, VT_COLS - 1);
        } else if (b == 1) {           /* start..cursor */
            for (r = 0; r < t->row; r++)
                erase_cells(t, r, 0, VT_COLS - 1);
            erase_cells(t, t->row, 0, t->col);
        } else {                       /* whole screen */
            for (r = 0; r < VT_ROWS; r++)
                erase_cells(t, r, 0, VT_COLS - 1);
        }
        t->dirty = 1;
        break;
    case 'K':                          /* EL */
        b = t->params[0];
        c0 = 0; c1 = VT_COLS - 1;
        if (b == 0)       c0 = t->col;          /* cursor..eol */
        else if (b == 1)  c1 = t->col;          /* start..cursor */
        erase_cells(t, t->row, c0, c1);
        t->dirty = 1;
        break;
    case 'r':                          /* DECSTBM */
        b = p_or(t, 1, VT_ROWS);
        if (a < b && b <= VT_ROWS) {
            t->top = (unsigned char)(a - 1);
            t->bot = (unsigned char)(b - 1);
            t->row = t->top;           /* DECSTBM homes the cursor */
            t->col = 0;
            t->wrap_pending = 0;
        }
        break;
    case 's':                          /* ANSI save cursor */
        t->save_row = t->row; t->save_col = t->col; t->save_attr = t->cur_attr;
        break;
    case 'u':                          /* ANSI restore cursor */
        t->row = t->save_row; t->col = t->save_col; t->cur_attr = t->save_attr;
        t->wrap_pending = 0;
        break;
    case 'm':                          /* SGR */
        for (i = 0; i <= t->pidx; i++) {
            switch (t->params[i]) {
            case 0:  t->cur_attr = VT_A_NORMAL; break;
            case 1:  t->cur_attr |= VT_A_BOLD; break;
            case 4:  t->cur_attr |= VT_A_UNDERLINE; break;
            case 5:  t->cur_attr |= VT_A_BLINK; break;
            case 7:  t->cur_attr |= VT_A_REVERSE; break;
            case 21:
            case 22: t->cur_attr &= (unsigned char)~VT_A_BOLD; break;
            case 24: t->cur_attr &= (unsigned char)~VT_A_UNDERLINE; break;
            case 25: t->cur_attr &= (unsigned char)~VT_A_BLINK; break;
            case 27: t->cur_attr &= (unsigned char)~VT_A_REVERSE; break;
            default: break;            /* colours etc. ignored (monochrome) */
            }
        }
        break;
    default:
        break;                         /* unhandled CSI: ignore */
    }
}

/* ---- public API ----------------------------------------------------- */

void vt_init(VT *t)
{
    unsigned char r;
    for (r = 0; r < VT_ROWS; r++)
        erase_cells(t, r, 0, VT_COLS - 1);
    t->row = t->col = 0;
    t->cur_attr = VT_A_NORMAL;
    t->save_row = t->save_col = t->save_attr = 0;
    t->top = 0;
    t->bot = VT_ROWS - 1;
    t->wrap_pending = 0;
    t->dirty = 1;
    t->state = VT_ST_GROUND;
    t->pidx = 0;
    t->seen = 0;
    t->priv = 0;
}

static void csi_begin(VT *t)
{
    unsigned char i;
    for (i = 0; i < VT_NPARAMS; i++) t->params[i] = 0;
    t->pidx = 0;
    t->seen = 0;
    t->priv = 0;
    t->state = VT_ST_CSI;
}

void vt_putc(VT *t, unsigned char c)
{
    switch (t->state) {
    case VT_ST_GROUND:
        if (c == 0x1B)            t->state = VT_ST_ESC;
        else if (c < 0x20)        ctrl(t, c);
        else                      put_glyph(t, c);
        break;

    case VT_ST_ESC:
        switch (c) {
        case '[': csi_begin(t); break;
        case '7': t->save_row = t->row; t->save_col = t->col;
                  t->save_attr = t->cur_attr; t->state = VT_ST_GROUND; break;
        case '8': t->row = t->save_row; t->col = t->save_col;
                  t->cur_attr = t->save_attr; t->wrap_pending = 0;
                  t->state = VT_ST_GROUND; break;
        case 'D': index_down(t); t->state = VT_ST_GROUND; break;
        case 'M': index_up(t);   t->state = VT_ST_GROUND; break;
        case 'E': t->col = 0; index_down(t); t->state = VT_ST_GROUND; break;
        case 'c': vt_init(t); break;          /* RIS (resets state) */
        default:  t->state = VT_ST_GROUND; break;  /* charset selects etc. */
        }
        break;

    case VT_ST_CSI:
        if (c >= '0' && c <= '9') {
            t->params[t->pidx] = t->params[t->pidx] * 10u + (unsigned int)(c - '0');
            t->seen = 1;
        } else if (c == ';') {
            if (t->pidx < VT_NPARAMS - 1) t->pidx++;
            t->seen = 1;
        } else if (c == '?') {
            t->priv = 1;
        } else if (c >= 0x40 && c <= 0x7E) {  /* final byte */
            csi_dispatch(t, c);
            t->state = VT_ST_GROUND;
        }
        /* intermediates (0x20-0x2F) are swallowed, staying in CSI */
        break;

    default:
        t->state = VT_ST_GROUND;
        break;
    }
}

unsigned char vt_char(const VT *t, unsigned char r, unsigned char c)
{
    return t->ch[r][c];
}

unsigned char vt_attr(const VT *t, unsigned char r, unsigned char c)
{
    return t->attr[r][c];
}
