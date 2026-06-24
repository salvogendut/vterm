/*
 * telnet.h - minimal Telnet (RFC 854) input filter for vterm
 *
 * PerryFi (and most TCP "modems") connect raw, so a telnet host's IAC
 * negotiation bytes arrive in the data stream and would otherwise render as
 * garbage. This filter sits between the serial input and the VT100 engine: it
 * swallows IAC command sequences, unescapes IAC IAC to a literal 0xFF, and
 * queues the WILL/WONT/DO/DONT replies a dumb terminal should send (drained by
 * the caller back to the host). It is transparent to non-telnet streams that
 * contain no 0xFF bytes.
 *
 * Portable C: builds with gcc (host tests) and SDCC (Z80 target). No malloc.
 */
#ifndef VTERM_TELNET_H
#define VTERM_TELNET_H

#define TN_RESP_SZ 24

typedef struct {
    unsigned char state;
    unsigned char cmd;                 /* pending WILL/WONT/DO/DONT */
    unsigned char resp[TN_RESP_SZ];    /* outbound replies (-> host) */
    unsigned char resp_head, resp_tail;
} Telnet;

void telnet_init(Telnet *t);
int  telnet_filter(Telnet *t, unsigned char c); /* data byte 0..255, or -1 */
int  telnet_resp_getc(Telnet *t);               /* reply byte, or -1 */

#endif /* VTERM_TELNET_H */
