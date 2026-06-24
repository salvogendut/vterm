#!/usr/bin/env python3
"""Telnet-host peer for the IAC-filter test.

Sends telnet IAC negotiation (WILL ECHO, WILL SGA, DO TTYPE) followed by an
ANSI screen. With the filter working, vterm should strip the IAC bytes (no
garbage on screen) and reply with the right WILL/WONT/DO/DONT. Logs any IAC
reply it receives from vterm.

Usage: telnet_peer.py <pty-path> [duration-seconds]
"""
import os, sys, time, termios

path = sys.argv[1]
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0

fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
try:
    a = termios.tcgetattr(fd); a[0] = a[1] = a[3] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
except Exception:
    pass
os.set_blocking(fd, False)

IAC, WILL, DO = 255, 251, 253
ECHO, SGA, TTYPE = 1, 3, 24
E = b"\x1b"

def welcome():
    s = bytes([IAC, WILL, ECHO, IAC, WILL, SGA, IAC, DO, TTYPE])
    s += E + b"[2J" + E + b"[1;25HTELNET HOST (IAC filtered)"
    s += E + b"[3;5HIf this line is clean, the IAC bytes were stripped."
    s += E + b"[5;5H$ "
    return s

deadline = time.time() + dur
last = 0.0
while time.time() < deadline:
    now = time.time()
    if now - last > 2.0:
        try:
            os.write(fd, welcome())
        except OSError:
            break
        last = now
    try:
        d = os.read(fd, 256)
        if d:
            if 255 in d:
                sys.stderr.write("peer got IAC reply: %r\n" % d)
                sys.stderr.flush()
            # A real host processes the client's IAC commands and does NOT
            # echo them; echo only the data bytes (here there are none).
            data, i = bytearray(), 0
            while i < len(d):
                if d[i] == 255:
                    nxt = d[i + 1] if i + 1 < len(d) else 0
                    i += 2 if nxt in (255, 250, 240) or nxt < 251 else 3
                    if nxt == 255:
                        data.append(255)
                else:
                    data.append(d[i]); i += 1
            if data:
                os.write(fd, bytes(data))
    except BlockingIOError:
        pass
    except OSError:
        break
    time.sleep(0.02)
