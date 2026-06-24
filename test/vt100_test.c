/*
 * vt100_test.c - host (gcc) unit tests for the VT100 engine.
 *
 * Build & run:  make test-vt100
 * Feeds escape sequences into the engine and asserts the resulting screen
 * model. Run with an argument (any) to also dump the grid for a visual look.
 */
#include "../src/vt100.h"
#include "../src/telnet.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

static void feed(VT *t, const char *s)
{
    while (*s) vt_putc(t, (unsigned char)*s++);
}

/* Compare a row's text (trailing blanks ignored up to the given expected). */
static void expect_row(VT *t, int r, const char *want, const char *label)
{
    char got[VT_COLS + 1];
    int c, n = (int)strlen(want);
    for (c = 0; c < VT_COLS; c++) got[c] = (char)vt_char(t, (unsigned char)r, (unsigned char)c);
    got[VT_COLS] = '\0';
    checks++;
    if (strncmp(got, want, (size_t)n) != 0) {
        failures++;
        printf("FAIL %-28s row %d\n   want: \"%s\"\n   got : \"%.*s\"\n",
               label, r, want, n, got);
    }
}

static void expect_int(int got, int want, const char *label)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %-28s want %d got %d\n", label, want, got);
    }
}

/* Drain the engine's reply queue and compare to the expected bytes. */
static void expect_resp(VT *t, const char *want, const char *label)
{
    char got[32];
    int n = 0, ch;
    while ((ch = vt_resp_getc(t)) >= 0 && n < 31)
        got[n++] = (char)ch;
    got[n] = '\0';
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %-28s want \"%s\" got \"%s\"\n", label, want, got);
    }
}

static void dump(VT *t)
{
    int r, c;
    printf("   +"); for (c = 0; c < VT_COLS; c++) putchar('-'); printf("+\n");
    for (r = 0; r < VT_ROWS; r++) {
        printf("%2d |", r);
        for (c = 0; c < VT_COLS; c++) {
            unsigned char ch = vt_char(t, (unsigned char)r, (unsigned char)c);
            putchar(ch < 0x20 ? '.' : ch);
        }
        printf("|\n");
    }
    printf("   +"); for (c = 0; c < VT_COLS; c++) putchar('-'); printf("+\n");
    printf("   cursor=(%d,%d) attr=0x%02X\n", t->row, t->col, t->cur_attr);
}

int main(int argc, char **argv)
{
    VT t;

    /* plain text + cursor advance */
    vt_init(&t);
    feed(&t, "Hello, world");
    expect_row(&t, 0, "Hello, world", "plain text");
    expect_int(t.col, 12, "cursor col after text");
    expect_int(t.row, 0, "cursor row after text");

    /* CR/LF */
    vt_init(&t);
    feed(&t, "abc\r\ndef");
    expect_row(&t, 0, "abc", "line 0 before crlf");
    expect_row(&t, 1, "def", "line 1 after crlf");
    expect_int(t.row, 1, "row after lf");
    expect_int(t.col, 3, "col after lf+text");

    /* backspace + tab */
    vt_init(&t);
    feed(&t, "abcd\b\bXY");
    expect_row(&t, 0, "abXY", "backspace overwrite");
    vt_init(&t);
    feed(&t, "a\tb");
    expect_int(vt_char(&t, 0, 8), 'b', "tab to column 8");

    /* CUP absolute positioning */
    vt_init(&t);
    feed(&t, "\033[3;5HX");
    expect_int(vt_char(&t, 2, 4), 'X', "CUP places glyph");
    expect_int(t.row, 2, "CUP row");
    expect_int(t.col, 5, "CUP col after glyph");

    /* cursor moves A/B/C/D */
    vt_init(&t);
    feed(&t, "\033[10;10H\033[3A\033[2D*");
    expect_int(t.row, 6, "CUU row");           /* 9 - 3 = 6 */
    expect_int(vt_char(&t, 6, 7), '*', "CUB then glyph at col 7");

    /* ED 2 clears screen */
    vt_init(&t);
    feed(&t, "junk\r\nmore");
    feed(&t, "\033[2J");
    expect_row(&t, 0, "    ", "ED2 clears row0");
    expect_row(&t, 1, "    ", "ED2 clears row1");

    /* EL 0 clears to end of line */
    vt_init(&t);
    feed(&t, "ABCDEFGH\033[1;4H\033[K");
    expect_row(&t, 0, "ABC", "EL0 keeps prefix");
    expect_int(vt_char(&t, 0, 3), ' ', "EL0 erased col 3");
    expect_int(vt_char(&t, 0, 7), ' ', "EL0 erased col 7");

    /* SGR attributes */
    vt_init(&t);
    feed(&t, "\033[1mA\033[0mB");
    expect_int(vt_attr(&t, 0, 0), VT_A_BOLD, "SGR bold cell");
    expect_int(vt_attr(&t, 0, 1), VT_A_NORMAL, "SGR reset cell");

    /* auto-wrap: deferred at last column */
    vt_init(&t);
    {
        int i;
        for (i = 0; i < VT_COLS; i++) vt_putc(&t, (unsigned char)('0' + (i % 10)));
        expect_int(t.row, 0, "still row0 at last col (deferred)");
        vt_putc(&t, 'W');
        expect_int(t.row, 1, "wrapped to row1 on next char");
        expect_int(vt_char(&t, 1, 0), 'W', "wrapped glyph at (1,0)");
    }

    /* scrolling: fill all rows, one more LF scrolls */
    vt_init(&t);
    {
        int r;
        for (r = 0; r < VT_ROWS; r++) {
            char buf[8];
            sprintf(buf, "L%d", r);
            feed(&t, buf);
            if (r < VT_ROWS - 1) feed(&t, "\r\n");
        }
        expect_row(&t, 0, "L0", "before scroll, row0=L0");
        feed(&t, "\r\n");                  /* LF at bottom -> scroll */
        expect_row(&t, 0, "L1", "after scroll, row0=L1");
        expect_row(&t, VT_ROWS - 1, "  ", "after scroll, last row blank");
    }

    /* DECSTBM scroll region + RI */
    vt_init(&t);
    feed(&t, "\033[2;4r");                /* region rows 2..4 (0-based 1..3) */
    expect_int(t.top, 1, "DECSTBM top");
    expect_int(t.bot, 3, "DECSTBM bot");
    expect_int(t.row, 0, "DECSTBM homes to screen origin");

    /* DECSC / DECRC */
    vt_init(&t);
    feed(&t, "\033[5;5H\0337\033[10;10HX\0338Y");
    expect_int(vt_char(&t, 4, 4), 'Y', "DECRC restored to (4,4)");
    expect_int(vt_char(&t, 9, 9), 'X', "glyph left at (9,9)");

    /* --- wider coverage ------------------------------------------- */

    /* SGR 256-colour / truecolour params must be skipped, not parsed as
     * attributes (38;5;N would otherwise turn on BLINK via the 5). */
    vt_init(&t);
    feed(&t, "\033[38;5;231mX\033[48;2;1;2;3mY");
    expect_int(vt_attr(&t, 0, 0), VT_A_NORMAL, "SGR 38;5 skipped");
    expect_int(vt_attr(&t, 0, 1), VT_A_NORMAL, "SGR 48;2 skipped");

    /* DEC special-graphics line drawing -> ASCII */
    vt_init(&t);
    feed(&t, "\033(0lqk\033(BZ");
    expect_row(&t, 0, "+-+Z", "line drawing then ASCII");

    /* ICH / DCH / ECH */
    vt_init(&t);
    feed(&t, "ABCDE\033[1;3H\033[2@");
    expect_row(&t, 0, "AB  CDE", "ICH inserts blanks");
    vt_init(&t);
    feed(&t, "ABCDEF\033[1;2H\033[2P");
    expect_row(&t, 0, "ADEF", "DCH deletes chars");
    vt_init(&t);
    feed(&t, "ABCDEF\033[1;3H\033[3X");
    expect_row(&t, 0, "AB   F", "ECH erases chars");

    /* IL / DL within the scroll region */
    vt_init(&t);
    feed(&t, "L0\r\nL1\r\nL2\r\nL3\033[1;1H\033[L");
    expect_row(&t, 0, "  ", "IL blanks row 0");
    expect_row(&t, 1, "L0", "IL pushed L0 down");
    vt_init(&t);
    feed(&t, "L0\r\nL1\r\nL2\r\nL3\033[1;1H\033[M");
    expect_row(&t, 0, "L1", "DL pulled L1 up");

    /* CNL / CPL */
    vt_init(&t);
    feed(&t, "\033[5;5H\033[2EX");
    expect_int(vt_char(&t, 6, 0), 'X', "CNL to (6,0)");
    vt_init(&t);
    feed(&t, "\033[5;5H\033[2FY");
    expect_int(vt_char(&t, 2, 0), 'Y', "CPL to (2,0)");

    /* SU / SD */
    vt_init(&t);
    feed(&t, "TOP\r\nMID\033[1;1H\033[1S");
    expect_row(&t, 0, "MID", "SU scrolled up");

    /* autowrap off: stay on last column */
    vt_init(&t);
    feed(&t, "\033[?7l");
    {
        int i;
        for (i = 0; i < VT_COLS + 5; i++) vt_putc(&t, 'x');
        expect_int(t.row, 0, "no wrap with DECAWM off");
        expect_int(t.col, VT_COLS - 1, "stuck at last column");
    }

    /* DECTCEM cursor hide/show */
    vt_init(&t);
    feed(&t, "\033[?25l");
    expect_int(vt_cursor_visible(&t), 0, "cursor hidden");
    feed(&t, "\033[?25h");
    expect_int(vt_cursor_visible(&t), 1, "cursor shown");

    /* origin mode: CUP is relative to the scroll region top */
    vt_init(&t);
    feed(&t, "\033[5;10r\033[?6h\033[1;1HO");
    expect_int(vt_char(&t, 4, 0), 'O', "origin-mode home at region top");

    /* tab stops: clear all, set one, tab to it */
    vt_init(&t);
    feed(&t, "\033[3g\033[1;5H\033H\033[1;1H\tT");
    expect_int(vt_char(&t, 0, 4), 'T', "custom tab stop at col 4");

    /* DA report */
    vt_init(&t);
    feed(&t, "\033[c");
    expect_resp(&t, "\033[?1;0c", "DA reply");

    /* DSR cursor position report */
    vt_init(&t);
    feed(&t, "\033[3;5H\033[6n");
    expect_resp(&t, "\033[3;5R", "DSR cursor report");

    /* alternate screen: ?1049 saves/clears, restores on exit */
    vt_init(&t);
    feed(&t, "PRIMARY\033[5;5Hx");          /* primary content + cursor (4,5) */
    feed(&t, "\033[?1049h");                /* enter alt: blank, cursor saved */
    expect_row(&t, 0, "  ", "alt screen starts blank");
    feed(&t, "\033[1;1HALT-TEXT");
    expect_row(&t, 0, "ALT-TEXT", "alt content visible");
    feed(&t, "\033[?1049l");                /* leave alt: restore primary */
    expect_row(&t, 0, "PRIMARY", "primary restored on exit");
    expect_int(t.row, 4, "cursor row restored");
    expect_int(t.col, 5, "cursor col restored");

    /* --- telnet IAC filter ---------------------------------------- */
    {
        Telnet tn;
        char out[64];
        char rep[64];
        int  n, rn, i, d, rc;
        /* IAC stripped; IAC IAC -> literal 0xFF; data passes through */
        static const unsigned char in1[] = {
            'A', 'B',
            255, 251, 1,        /* IAC WILL ECHO  -> reply DO ECHO   */
            'C',
            255, 253, 3,        /* IAC DO SGA     -> reply WILL SGA  */
            255, 255,           /* IAC IAC        -> literal 0xFF    */
            'D',
            255, 250, 24, 'x', 255, 240,  /* IAC SB TTYPE x IAC SE (swallowed) */
            'E'
        };
        telnet_init(&tn);
        n = 0;
        for (i = 0; i < (int)sizeof(in1); i++) {
            d = telnet_filter(&tn, in1[i]);
            if (d >= 0) out[n++] = (char)d;
        }
        out[n] = '\0';
        checks++;
        if (!(out[0]=='A' && out[1]=='B' && out[2]=='C' &&
              (unsigned char)out[3]==0xFF && out[4]=='D' && out[5]=='E' && n==6)) {
            failures++;
            printf("FAIL %-28s got %d bytes\n", "telnet strips IAC", n);
        }
        /* replies: DO ECHO, WILL SGA */
        rn = 0;
        while ((rc = telnet_resp_getc(&tn)) >= 0) rep[rn++] = (char)rc;
        checks++;
        if (!(rn==6 && (unsigned char)rep[0]==255 && (unsigned char)rep[1]==253 && rep[2]==1 &&
                      (unsigned char)rep[3]==255 && (unsigned char)rep[4]==251 && rep[5]==3)) {
            failures++;
            printf("FAIL %-28s got %d reply bytes\n", "telnet WILL/DO replies", rn);
        }
        /* a host DO we refuse -> WONT */
        telnet_init(&tn);
        telnet_filter(&tn, 255); telnet_filter(&tn, 253); telnet_filter(&tn, 31); /* DO NAWS */
        rn = 0;
        while ((rc = telnet_resp_getc(&tn)) >= 0) rep[rn++] = (char)rc;
        checks++;
        if (!(rn==3 && (unsigned char)rep[1]==252 && (unsigned char)rep[2]==31)) { /* WONT NAWS */
            failures++;
            printf("FAIL %-28s got %d\n", "telnet refuses DO NAWS", rn);
        }
    }

    (void)argv;
    printf("\n%d checks, %d failure(s)\n", checks, failures);
    if (argc > 1) dump(&t);
    return failures ? 1 : 0;
}
