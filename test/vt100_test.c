/*
 * vt100_test.c - host (gcc) unit tests for the VT100 engine.
 *
 * Build & run:  make test-vt100
 * Feeds escape sequences into the engine and asserts the resulting screen
 * model. Run with an argument (any) to also dump the grid for a visual look.
 */
#include "../src/vt100.h"
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
    expect_int(t.row, 1, "DECSTBM homes cursor");

    /* DECSC / DECRC */
    vt_init(&t);
    feed(&t, "\033[5;5H\0337\033[10;10HX\0338Y");
    expect_int(vt_char(&t, 4, 4), 'Y', "DECRC restored to (4,4)");
    expect_int(vt_char(&t, 9, 9), 'X', "glyph left at (9,9)");

    (void)argv;
    printf("\n%d checks, %d failure(s)\n", checks, failures);
    if (argc > 1) dump(&t);
    return failures ? 1 : 0;
}
