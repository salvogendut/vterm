/*
 * render_vt52.c - VT100 screen model -> Amstrad CP/M Plus VT52 console.
 *
 * Shared by the PCW and the CPC: both run Amstrad CP/M Plus, whose console
 * speaks the same VT52 dialect (ESC Y position, ESC E clear, ESC K erase EOL,
 * ESC M/L line delete/insert for scrolling, ESC p/q inverse, ESC e/f cursor).
 * See render.h.
 */
#include "render.h"
#include "cpm.h"
#include <string.h>

#define ESC 0x1B

/* Attributes the PCW console can show; we render only these. */
#define REND_ATTRS (VT_A_REVERSE | VT_A_BOLD)

/* Shadow of what is currently on the console. */
static unsigned char sh_ch[VT_ROWS][VT_COLS];
static unsigned char sh_attr[VT_ROWS][VT_COLS];

/* Tracked console state, to avoid redundant ESC sequences. */
static unsigned char cur_r, cur_c;    /* where the console cursor sits     */
static unsigned char cur_known;       /* is cur_r/cur_c trustworthy        */
static unsigned char cur_attr;        /* attribute currently selected      */
static unsigned char cur_shown;       /* console cursor currently visible  */

static void esc2(char a)
{
    conout(ESC);
    conout(a);
}

static void move_to(unsigned char r, unsigned char c)
{
    conout(ESC);
    conout('Y');
    conout((char)(r + 32));
    conout((char)(c + 32));
    cur_r = r;
    cur_c = c;
    cur_known = 1;
}

/* Emit only the transitions needed to reach attribute `a`. */
static void set_attr(unsigned char a)
{
    unsigned char diff = a ^ cur_attr;
    if (diff & VT_A_REVERSE)
        esc2((a & VT_A_REVERSE) ? 'p' : 'q');
    if (diff & VT_A_BOLD)
        esc2((a & VT_A_BOLD) ? 'r' : 'u');
    cur_attr = a;
}

void render_init(void)
{
    unsigned char r, c;
    esc2('E');                         /* clear + home */
    esc2('q');                         /* normal video */
    esc2('u');                         /* not bright   */
    esc2('e');                         /* cursor on    */
    cur_attr = VT_A_NORMAL;
    cur_shown = 1;
    /* ESC E does not reliably wipe the whole PCW screen, so explicitly erase
     * each of our rows (ESC K = erase to end of line) to start from a known
     * blank console that matches the shadow. */
    for (r = 0; r < VT_ROWS; r++) {
        move_to(r, 0);
        esc2('K');
        for (c = 0; c < VT_COLS; c++) {
            sh_ch[r][c]   = ' ';
            sh_attr[r][c] = VT_A_NORMAL;
        }
    }
    move_to(0, 0);
}

/* Mirror a whole-screen scroll onto the console with native line delete/insert
 * (ESC M / ESC L), and shift the shadow to match, so only genuinely-new cells
 * repaint afterwards instead of the whole screen. */
static void apply_scroll(signed char sc)
{
    unsigned char n, k, r;
    n = (sc > 0) ? (unsigned char)sc : (unsigned char)(-sc);
    if (n > VT_ROWS) n = VT_ROWS;

    if (cur_attr != VT_A_NORMAL)
        set_attr(VT_A_NORMAL);          /* blanked line should be normal */
    move_to(0, 0);
    for (k = 0; k < n; k++)
        esc2(sc > 0 ? 'M' : 'L');       /* delete line = up, insert = down */

    if (sc > 0) {                       /* scroll up: drop top n rows */
        for (r = 0; (unsigned char)(r + n) < VT_ROWS; r++) {
            memcpy(sh_ch[r],   sh_ch[r + n],   VT_COLS);
            memcpy(sh_attr[r], sh_attr[r + n], VT_COLS);
        }
        for (r = (unsigned char)(VT_ROWS - n); r < VT_ROWS; r++) {
            unsigned char cc;
            for (cc = 0; cc < VT_COLS; cc++) { sh_ch[r][cc] = ' '; sh_attr[r][cc] = VT_A_NORMAL; }
        }
    } else {                            /* scroll down: shift rows down n */
        for (r = VT_ROWS - 1; r >= n; r--) {
            memcpy(sh_ch[r],   sh_ch[r - n],   VT_COLS);
            memcpy(sh_attr[r], sh_attr[r - n], VT_COLS);
            if (r == 0) break;
        }
        for (r = 0; r < n; r++) {
            unsigned char cc;
            for (cc = 0; cc < VT_COLS; cc++) { sh_ch[r][cc] = ' '; sh_attr[r][cc] = VT_A_NORMAL; }
        }
    }
    cur_r = 0; cur_c = 0; cur_known = 1;
}

void render_flush(VT *t)
{
    unsigned char r, c, ch, at;

    if (t->scroll) {
        apply_scroll(t->scroll);
        t->scroll = 0;
    }

    for (r = 0; r < VT_ROWS; r++) {
        for (c = 0; c < VT_COLS; c++) {
            ch = t->ch[r][c];
            at = (unsigned char)(t->attr[r][c] & REND_ATTRS);
            if (ch == sh_ch[r][c] && at == sh_attr[r][c])
                continue;              /* unchanged */

            if (!cur_known || cur_r != r || cur_c != c)
                move_to(r, c);
            if (at != cur_attr)
                set_attr(at);
            conout((char)ch);

            sh_ch[r][c]   = ch;
            sh_attr[r][c] = at;

            /* The console cursor auto-advanced; last column is ambiguous. */
            if (cur_c + 1 < VT_COLS)
                cur_c++;
            else
                cur_known = 0;
        }
    }

    /* Leave the visible cursor where the model wants it, in normal video. */
    if (cur_attr != VT_A_NORMAL)
        set_attr(VT_A_NORMAL);
    move_to(t->row, (unsigned char)(t->col < VT_COLS ? t->col : VT_COLS - 1));

    /* Track the host's cursor-visibility mode (DECTCEM). */
    if (vt_cursor_visible(t) != cur_shown) {
        cur_shown = vt_cursor_visible(t);
        esc2(cur_shown ? 'e' : 'f');   /* ESC e = on, ESC f = off */
    }
}
