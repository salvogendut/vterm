/*
 * vt100.c - VT100/ANSI terminal engine. See vt100.h.
 *
 * Implements a broad practical VT100/ANSI subset:
 *   - text with auto-wrap (deferred last-column quirk, DECAWM-aware)
 *   - C0 controls: BS, HT (tab stops), LF/VT/FF, CR, SO/SI
 *   - ESC: IND, RI, NEL, HTS, DECSC/DECRC, RIS, charset designation ESC(/ESC)
 *   - CSI cursor: CUU/CUD/CUF/CUB, CNL/CPL, CHA, VPA, CUP/HVP
 *   - CSI erase/edit: ED, EL, ECH, ICH, DCH, IL, DL, SU, SD
 *   - CSI SGR (bold/underline/blink/reverse, with 256/truecolour params skipped)
 *   - CSI modes: DECSTBM, DECAWM(?7), DECOM(?6), DECTCEM(?25)
 *   - CSI reports: DA (ESC[c) and DSR (ESC[5n/6n) queued for the caller
 *   - DEC special-graphics line drawing mapped to ASCII
 */
#include "vt100.h"
#include <string.h>

/* ---- reply ring (engine -> host) ------------------------------------ */

static void resp_push(VT *t, unsigned char c)
{
    unsigned char nt = (unsigned char)((t->resp_tail + 1) % VT_RESP_SZ);
    if (nt != t->resp_head) {            /* drop on overflow */
        t->resp[t->resp_tail] = c;
        t->resp_tail = nt;
    }
}

static void resp_str(VT *t, const char *s)
{
    while (*s) resp_push(t, (unsigned char)*s++);
}

static void resp_num(VT *t, unsigned int n)
{
    char buf[6];
    unsigned char i = 0;
    if (n == 0) { resp_push(t, '0'); return; }
    while (n) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i) resp_push(t, (unsigned char)buf[--i]);
}

int vt_resp_getc(VT *t)
{
    unsigned char c;
    if (t->resp_head == t->resp_tail) return -1;
    c = t->resp[t->resp_head];
    t->resp_head = (unsigned char)((t->resp_head + 1) % VT_RESP_SZ);
    return (int)c;
}

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

static void blank_row(VT *t, unsigned char r)
{
    erase_cells(t, r, 0, VT_COLS - 1);
}

static void scroll_up(VT *t, unsigned char n)
{
    unsigned char r;
    if (n == 0) return;
    if (n > (unsigned char)(t->bot - t->top + 1)) n = (unsigned char)(t->bot - t->top + 1);
    for (r = t->top; (unsigned char)(r + n) <= t->bot; r++) {
        memcpy(t->ch[r],   t->ch[r + n],   VT_COLS);
        memcpy(t->attr[r], t->attr[r + n], VT_COLS);
    }
    for (r = (unsigned char)(t->bot - n + 1); r <= t->bot; r++)
        blank_row(t, r);
    t->dirty = 1;
}

static void scroll_down(VT *t, unsigned char n)
{
    unsigned char r;
    if (n == 0) return;
    if (n > (unsigned char)(t->bot - t->top + 1)) n = (unsigned char)(t->bot - t->top + 1);
    for (r = t->bot; r >= (unsigned char)(t->top + n); r--) {
        memcpy(t->ch[r],   t->ch[r - n],   VT_COLS);
        memcpy(t->attr[r], t->attr[r - n], VT_COLS);
        if (r == 0) break;
    }
    for (r = t->top; r <= (unsigned char)(t->top + n - 1); r++)
        blank_row(t, r);
    t->dirty = 1;
}

/* Insert n blank lines at the cursor row, within the scroll region. */
static void insert_lines(VT *t, unsigned char n)
{
    unsigned char r;
    if (t->row < t->top || t->row > t->bot) return;
    if (n > (unsigned char)(t->bot - t->row + 1)) n = (unsigned char)(t->bot - t->row + 1);
    for (r = t->bot; r >= (unsigned char)(t->row + n); r--) {
        memcpy(t->ch[r],   t->ch[r - n],   VT_COLS);
        memcpy(t->attr[r], t->attr[r - n], VT_COLS);
        if (r == 0) break;
    }
    for (r = t->row; r <= (unsigned char)(t->row + n - 1); r++)
        blank_row(t, r);
    t->dirty = 1;
}

/* Delete n lines at the cursor row, within the scroll region. */
static void delete_lines(VT *t, unsigned char n)
{
    unsigned char r;
    if (t->row < t->top || t->row > t->bot) return;
    if (n > (unsigned char)(t->bot - t->row + 1)) n = (unsigned char)(t->bot - t->row + 1);
    for (r = t->row; (unsigned char)(r + n) <= t->bot; r++) {
        memcpy(t->ch[r],   t->ch[r + n],   VT_COLS);
        memcpy(t->attr[r], t->attr[r + n], VT_COLS);
    }
    for (r = (unsigned char)(t->bot - n + 1); r <= t->bot; r++)
        blank_row(t, r);
    t->dirty = 1;
}

/* Insert n blank chars at the cursor (shift the rest of the row right). */
static void insert_chars(VT *t, unsigned char n)
{
    unsigned char c;
    if (n > (unsigned char)(VT_COLS - t->col)) n = (unsigned char)(VT_COLS - t->col);
    for (c = VT_COLS - 1; c >= (unsigned char)(t->col + n); c--) {
        t->ch[t->row][c]   = t->ch[t->row][c - n];
        t->attr[t->row][c] = t->attr[t->row][c - n];
        if (c == 0) break;
    }
    erase_cells(t, t->row, t->col, (unsigned char)(t->col + n - 1));
    t->dirty = 1;
}

/* Delete n chars at the cursor (shift the rest of the row left). */
static void delete_chars(VT *t, unsigned char n)
{
    unsigned char c;
    if (n > (unsigned char)(VT_COLS - t->col)) n = (unsigned char)(VT_COLS - t->col);
    for (c = t->col; (unsigned char)(c + n) < VT_COLS; c++) {
        t->ch[t->row][c]   = t->ch[t->row][c + n];
        t->attr[t->row][c] = t->attr[t->row][c + n];
    }
    erase_cells(t, t->row, (unsigned char)(VT_COLS - n), VT_COLS - 1);
    t->dirty = 1;
}

/* ---- charset: DEC special graphics -> ASCII line drawing ------------ */

static unsigned char dec_graphic(unsigned char c)
{
    switch (c) {
    case 'j': case 'k': case 'l': case 'm':   /* corners */
    case 'n': case 't': case 'u': case 'v': case 'w':  /* tees/cross */
    case '`': case '+':                        /* diamond / arrow */
        return '+';
    case 'q': case 'o': case 'p': case 'r':    /* horizontal scan lines */
        return '-';
    case 'x':                                  /* vertical */
        return '|';
    case 's': case '_':                        /* low scan / blank */
        return '_';
    case 'a':                                  /* checkerboard */
        return ':';
    case '~':                                  /* bullet */
        return '.';
    default:
        return c;                              /* leave unmapped glyphs */
    }
}

/* ---- glyph output --------------------------------------------------- */

static void index_down(VT *t)
{
    if (t->row == t->bot)
        scroll_up(t, 1);
    else if (t->row < VT_ROWS - 1)
        t->row++;
}

static void index_up(VT *t)
{
    if (t->row == t->top)
        scroll_down(t, 1);
    else if (t->row > 0)
        t->row--;
}

static void put_glyph(VT *t, unsigned char c)
{
    unsigned char active = t->gl ? t->g1 : t->g0;
    if (active == 1)
        c = dec_graphic(c);

    if (t->wrap_pending) {               /* deferred wrap from last column */
        t->col = 0;
        index_down(t);
        t->wrap_pending = 0;
    }
    t->ch[t->row][t->col]   = c;
    t->attr[t->row][t->col] = t->cur_attr;
    if (t->col + 1 >= VT_COLS) {
        if (t->autowrap)
            t->wrap_pending = 1;         /* sit on last column until next char */
        /* else: stay put, overwrite last column */
    } else {
        t->col++;
    }
    t->dirty = 1;
}

/* ---- cursor helpers ------------------------------------------------- */

static void clamp_cursor(VT *t)
{
    if (t->row >= VT_ROWS) t->row = VT_ROWS - 1;
    if (t->col >= VT_COLS) t->col = VT_COLS - 1;
    t->wrap_pending = 0;
}

/* ---- tab stops ------------------------------------------------------ */

static void tab_forward(VT *t, unsigned char n)
{
    while (n--) {
        unsigned char c = t->col;
        do { c++; } while (c < VT_COLS - 1 && !t->tabs[c]);
        t->col = c;
    }
    if (t->col >= VT_COLS) t->col = VT_COLS - 1;
    t->wrap_pending = 0;
}

static void tab_back(VT *t, unsigned char n)
{
    while (n--) {
        unsigned char c = t->col;
        while (c > 0) { c--; if (t->tabs[c]) break; }
        t->col = c;
    }
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
        tab_forward(t, 1);
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
    case 0x0E:                        /* SO  -> invoke G1 */
        t->gl = 1;
        break;
    case 0x0F:                        /* SI  -> invoke G0 */
        t->gl = 0;
        break;
    default:                          /* BEL and others: no display effect */
        break;
    }
}

/* ---- alternate screen ----------------------------------------------- */

/* Copy a whole 24x80 cell plane. Done row by row with an explicit length: a
 * single memcpy(..., sizeof(t->ch)) misbehaves under SDCC for a 2D-array
 * struct member referenced through a pointer. */
static void copy_plane(unsigned char dst[VT_ROWS][VT_COLS],
                       unsigned char src[VT_ROWS][VT_COLS])
{
    unsigned char r;
    for (r = 0; r < VT_ROWS; r++)
        memcpy(dst[r], src[r], VT_COLS);
}

static void enter_alt(VT *t)
{
    unsigned char r;
    if (t->on_alt) return;
    copy_plane(t->alt_ch,   t->ch);                 /* stash the primary */
    copy_plane(t->alt_attr, t->attr);
    for (r = 0; r < VT_ROWS; r++)
        blank_row(t, r);                            /* alt starts blank  */
    t->on_alt = 1;
    t->dirty = 1;
}

static void leave_alt(VT *t)
{
    if (!t->on_alt) return;
    copy_plane(t->ch,   t->alt_ch);                 /* restore the primary */
    copy_plane(t->attr, t->alt_attr);
    t->on_alt = 0;
    t->dirty = 1;
}

/* ---- DEC private modes (ESC[?Ph / ESC[?Pl) -------------------------- */

static void set_mode(VT *t, unsigned int m, unsigned char on)
{
    switch (m) {
    case 6:  t->origin_mode = on;            /* DECOM */
             t->row = on ? t->top : 0; t->col = 0; t->wrap_pending = 0; break;
    case 7:  t->autowrap = on; break;        /* DECAWM */
    case 25: t->cursor_visible = on; t->dirty = 1; break; /* DECTCEM */
    case 1048:                               /* save/restore cursor */
        if (on) { t->alt_save_row = t->row; t->alt_save_col = t->col;
                  t->alt_save_attr = t->cur_attr; }
        else    { t->row = t->alt_save_row; t->col = t->alt_save_col;
                  t->cur_attr = t->alt_save_attr; t->wrap_pending = 0; }
        break;
    case 1049:                               /* save cursor + alt screen */
        if (on) { t->alt_save_row = t->row; t->alt_save_col = t->col;
                  t->alt_save_attr = t->cur_attr; enter_alt(t); }
        else    { leave_alt(t); t->row = t->alt_save_row;
                  t->col = t->alt_save_col; t->cur_attr = t->alt_save_attr;
                  t->wrap_pending = 0; }
        break;
    case 47:                                 /* alternate screen */
    case 1047:
        if (on) enter_alt(t); else leave_alt(t);
        break;
    default: break;
    }
}

/* ---- CSI dispatch --------------------------------------------------- */

static unsigned int p_or(const VT *t, unsigned char i, unsigned int def)
{
    unsigned int v = (i < VT_NPARAMS) ? t->params[i] : 0;
    return v ? v : def;
}

static void do_sgr(VT *t)
{
    unsigned char i;
    for (i = 0; i <= t->pidx; i++) {
        unsigned int p = t->params[i];
        switch (p) {
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
        case 38:                          /* set fg colour: skip its args */
        case 48:                          /* set bg colour: skip its args */
            if (i + 1 <= t->pidx) {
                if (t->params[i + 1] == 5)       i += 2;   /* 5;N      */
                else if (t->params[i + 1] == 2)  i += 4;   /* 2;R;G;B  */
                else                             i += 1;
            }
            break;
        default: break;                   /* 2/8/30-37/39/40-47/90.. ignored (mono) */
        }
    }
}

static void csi_dispatch(VT *t, unsigned char f)
{
    unsigned int a = p_or(t, 0, 1);
    unsigned int b;
    unsigned char i, r, c0, c1;

    /* DEC private modes use '?' + h/l. */
    if (t->priv) {
        if (f == 'h' || f == 'l') {
            unsigned char on = (f == 'h');
            for (i = 0; i <= t->pidx; i++)
                set_mode(t, t->params[i], on);
        }
        return;
    }

    switch (f) {
    case 'A':                          /* CUU */
        t->row = (t->row > a) ? (unsigned char)(t->row - a) : 0;
        if (t->origin_mode && t->row < t->top) t->row = t->top;
        t->wrap_pending = 0;
        break;
    case 'B':                          /* CUD */
        t->row = (unsigned char)(t->row + a);
        if (t->origin_mode && t->row > t->bot) t->row = t->bot;
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
    case 'E':                          /* CNL - n lines down, col 0 */
        t->row = (unsigned char)(t->row + a);
        if (t->origin_mode && t->row > t->bot) t->row = t->bot;
        t->col = 0; clamp_cursor(t);
        break;
    case 'F':                          /* CPL - n lines up, col 0 */
        t->row = (t->row > a) ? (unsigned char)(t->row - a) : 0;
        if (t->origin_mode && t->row < t->top) t->row = t->top;
        t->col = 0; t->wrap_pending = 0;
        break;
    case 'G':                          /* CHA - column */
        t->col = (unsigned char)(a - 1);
        clamp_cursor(t);
        break;
    case 'd':                          /* VPA - row */
        t->row = (unsigned char)(a - 1);
        if (t->origin_mode) t->row = (unsigned char)(t->top + (a - 1));
        clamp_cursor(t);
        break;
    case 'H':                          /* CUP */
    case 'f':                          /* HVP */
        b = p_or(t, 1, 1);
        t->row = (unsigned char)(a - 1);
        if (t->origin_mode) {
            t->row = (unsigned char)(t->top + (a - 1));
            if (t->row > t->bot) t->row = t->bot;
        }
        t->col = (unsigned char)(b - 1);
        clamp_cursor(t);
        break;
    case 'I':                          /* CHT - forward tabs */
        tab_forward(t, (unsigned char)a);
        break;
    case 'Z':                          /* CBT - backward tabs */
        tab_back(t, (unsigned char)a);
        break;
    case 'J':                          /* ED */
        b = t->params[0];
        if (b == 0) {
            erase_cells(t, t->row, t->col, VT_COLS - 1);
            for (r = (unsigned char)(t->row + 1); r < VT_ROWS; r++)
                blank_row(t, r);
        } else if (b == 1) {
            for (r = 0; r < t->row; r++)
                blank_row(t, r);
            erase_cells(t, t->row, 0, t->col);
        } else {
            for (r = 0; r < VT_ROWS; r++)
                blank_row(t, r);
        }
        t->dirty = 1;
        break;
    case 'K':                          /* EL */
        b = t->params[0];
        c0 = 0; c1 = VT_COLS - 1;
        if (b == 0)       c0 = t->col;
        else if (b == 1)  c1 = t->col;
        erase_cells(t, t->row, c0, c1);
        t->dirty = 1;
        break;
    case 'X':                          /* ECH - erase n chars */
        c1 = (unsigned char)(t->col + a - 1);
        if (c1 >= VT_COLS) c1 = VT_COLS - 1;
        erase_cells(t, t->row, t->col, c1);
        t->dirty = 1;
        break;
    case '@':                          /* ICH */
        insert_chars(t, (unsigned char)a);
        break;
    case 'P':                          /* DCH */
        delete_chars(t, (unsigned char)a);
        break;
    case 'L':                          /* IL */
        insert_lines(t, (unsigned char)a);
        break;
    case 'M':                          /* DL */
        delete_lines(t, (unsigned char)a);
        break;
    case 'S':                          /* SU - scroll region up */
        scroll_up(t, (unsigned char)a);
        break;
    case 'T':                          /* SD - scroll region down */
        scroll_down(t, (unsigned char)a);
        break;
    case 'g':                          /* TBC - clear tab stops */
        if (t->params[0] == 3) {
            for (i = 0; i < VT_COLS; i++) t->tabs[i] = 0;
        } else {
            t->tabs[t->col] = 0;
        }
        break;
    case 'r':                          /* DECSTBM */
        b = p_or(t, 1, VT_ROWS);
        if (a < b && b <= VT_ROWS) {
            t->top = (unsigned char)(a - 1);
            t->bot = (unsigned char)(b - 1);
            t->row = t->origin_mode ? t->top : 0;
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
        do_sgr(t);
        break;
    case 'c':                          /* DA - device attributes */
        resp_str(t, "\033[?1;0c");      /* VT100, no options */
        break;
    case 'n':                          /* DSR */
        if (t->params[0] == 6) {        /* report cursor position */
            resp_str(t, "\033[");
            resp_num(t, (unsigned int)(t->row + 1));
            resp_push(t, ';');
            resp_num(t, (unsigned int)(t->col + 1));
            resp_push(t, 'R');
        } else if (t->params[0] == 5) { /* report status: OK */
            resp_str(t, "\033[0n");
        }
        break;
    default:
        break;                         /* unhandled CSI: ignore */
    }
}

/* ---- public API ----------------------------------------------------- */

void vt_init(VT *t)
{
    unsigned char r, c;
    for (r = 0; r < VT_ROWS; r++)
        blank_row(t, r);
    t->row = t->col = 0;
    t->cur_attr = VT_A_NORMAL;
    t->save_row = t->save_col = t->save_attr = 0;
    t->save_gl = t->save_g0 = t->save_g1 = 0;
    t->top = 0;
    t->bot = VT_ROWS - 1;
    t->wrap_pending = 0;
    t->dirty = 1;
    t->autowrap = 1;
    t->origin_mode = 0;
    t->cursor_visible = 1;
    t->g0 = t->g1 = t->gl = 0;
    t->on_alt = 0;
    t->alt_save_row = t->alt_save_col = t->alt_save_attr = 0;
    for (c = 0; c < VT_COLS; c++)
        t->tabs[c] = (unsigned char)(c && (c % 8) == 0);
    t->state = VT_ST_GROUND;
    t->pidx = 0;
    t->seen = 0;
    t->priv = 0;
    t->resp_head = t->resp_tail = 0;
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
        case '(': t->state = VT_ST_G0; break;
        case ')': t->state = VT_ST_G1; break;
        case '7': t->save_row = t->row; t->save_col = t->col;
                  t->save_attr = t->cur_attr;
                  t->save_gl = t->gl; t->save_g0 = t->g0; t->save_g1 = t->g1;
                  t->state = VT_ST_GROUND; break;
        case '8': t->row = t->save_row; t->col = t->save_col;
                  t->cur_attr = t->save_attr;
                  t->gl = t->save_gl; t->g0 = t->save_g0; t->g1 = t->save_g1;
                  t->wrap_pending = 0; t->state = VT_ST_GROUND; break;
        case 'D': index_down(t); t->state = VT_ST_GROUND; break;
        case 'M': index_up(t);   t->state = VT_ST_GROUND; break;
        case 'E': t->col = 0; index_down(t); t->state = VT_ST_GROUND; break;
        case 'H': t->tabs[t->col] = 1; t->state = VT_ST_GROUND; break; /* HTS */
        case 'c': vt_init(t); break;          /* RIS (resets state) */
        default:  t->state = VT_ST_GROUND; break;  /* ESC=, ESC>, etc. */
        }
        break;

    case VT_ST_G0:                            /* ESC ( <set> */
        t->g0 = (c == '0') ? 1 : 0;           /* 0=DEC gfx, else ASCII */
        t->state = VT_ST_GROUND;
        break;
    case VT_ST_G1:                            /* ESC ) <set> */
        t->g1 = (c == '0') ? 1 : 0;
        t->state = VT_ST_GROUND;
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

unsigned char vt_cursor_visible(const VT *t)
{
    return t->cursor_visible;
}
