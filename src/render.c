/*
 * render.c - VT100 screen model -> Amstrad PCW (VT52) console. See render.h.
 */
#include "render.h"
#include "cpm.h"

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
    cur_attr = VT_A_NORMAL;
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

void render_flush(VT *t)
{
    unsigned char r, c, ch, at;

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
}
