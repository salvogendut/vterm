/*
 * telnet.c - Telnet IAC input filter. See telnet.h.
 */
#include "telnet.h"

/* Telnet commands. */
#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240

/* Options we have an opinion about. */
#define OPT_ECHO 1
#define OPT_SGA  3   /* suppress go-ahead */

/* Filter states. */
#define S_DATA  0
#define S_IAC   1    /* saw IAC */
#define S_OPT   2    /* saw IAC WILL/WONT/DO/DONT, expecting the option */
#define S_SB    3    /* inside subnegotiation */
#define S_SBIAC 4    /* saw IAC inside subnegotiation */

static void push(Telnet *t, unsigned char c)
{
    unsigned char nt = (unsigned char)((t->resp_tail + 1) % TN_RESP_SZ);
    if (nt != t->resp_head) {
        t->resp[t->resp_tail] = c;
        t->resp_tail = nt;
    }
}

static void reply(Telnet *t, unsigned char cmd, unsigned char opt)
{
    push(t, IAC);
    push(t, cmd);
    push(t, opt);
}

void telnet_init(Telnet *t)
{
    t->state = S_DATA;
    t->cmd = 0;
    t->resp_head = t->resp_tail = 0;
}

int telnet_filter(Telnet *t, unsigned char c)
{
    switch (t->state) {
    case S_DATA:
        if (c == IAC) { t->state = S_IAC; return -1; }
        return (int)c;

    case S_IAC:
        if (c == IAC) { t->state = S_DATA; return (int)IAC; }  /* literal 0xFF */
        if (c == WILL || c == WONT || c == DO || c == DONT) {
            t->cmd = c; t->state = S_OPT; return -1;
        }
        if (c == SB) { t->state = S_SB; return -1; }
        t->state = S_DATA; return -1;          /* NOP/DM/GA/...: swallowed */

    case S_OPT:
        /* Reply only to the host's offers (WILL) and requests (DO); treat
         * WONT/DONT as acknowledgements and stay silent, so we never loop. */
        if (t->cmd == WILL)
            reply(t, (c == OPT_ECHO || c == OPT_SGA) ? DO : DONT, c);
        else if (t->cmd == DO)
            reply(t, (c == OPT_SGA) ? WILL : WONT, c);
        t->state = S_DATA; return -1;

    case S_SB:
        if (c == IAC) t->state = S_SBIAC;
        return -1;                              /* swallow subnegotiation */

    case S_SBIAC:
        if (c == SE)       t->state = S_DATA;   /* end of subnegotiation */
        else               t->state = S_SB;     /* IAC IAC or stray byte */
        return -1;

    default:
        t->state = S_DATA;
        return (int)c;
    }
}

int telnet_resp_getc(Telnet *t)
{
    unsigned char c;
    if (t->resp_head == t->resp_tail) return -1;
    c = t->resp[t->resp_head];
    t->resp_head = (unsigned char)((t->resp_head + 1) % TN_RESP_SZ);
    return (int)c;
}
