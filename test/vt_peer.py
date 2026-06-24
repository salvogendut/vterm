#!/usr/bin/env python3
"""VT100-sending peer for the vterm render test.

Plays the role of the remote host: connects to the emulator's serial PTY and
periodically sends a VT100/ANSI test pattern (clear, cursor-addressed text, a
box, inverse/bold runs, a few lines). vterm parses it into its screen model
and the renderer paints it onto the PCW (VT52) console; the screenshot should
show the pattern laid out correctly.

Usage: vt_peer.py <pty-path> [duration-seconds]
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

E = b"\x1b"

def gfx(s):                                          # wrap in DEC line-drawing
    return E + b"(0" + s + E + b"(B"

def demo():
    s = E + b"[2J"                                   # clear
    s += E + b"[1;28HVTERM VT100 RENDER DEMO"
    s += E + b"[3;5HNormal " + E + b"[7m INVERSE " + E + b"[0m " \
         + E + b"[1m BOLD " + E + b"[0m"
    # box drawn with DEC special graphics (l k m j corners, q -, x |)
    s += E + b"[5;10H" + gfx(b"l" + b"q" * 28 + b"k")
    for r in range(6, 9):
        s += ("\x1b[%d;10H" % r).encode() + gfx(b"x") \
             + b" " * 28 + gfx(b"x")
    s += E + b"[7;14HDEC line-drawing box"
    s += E + b"[9;10H" + gfx(b"m" + b"q" * 28 + b"j")
    s += E + b"[12;5HLine A\r\nLine B\r\nLine C"
    s += E + b"[20;1HReady (256-colour " + E + b"[38;5;82mtext ok" + E + b"[0m)."
    s += E + b"[6n"                                   # ask for cursor position
    return s

deadline = time.time() + dur
last = 0.0
while time.time() < deadline:
    now = time.time()
    if now - last > 2.0:
        try:
            os.write(fd, demo())
        except OSError:
            break
        last = now
    try:
        back = os.read(fd, 256)   # vterm's replies (e.g. DSR cursor report)
        if back and b"R" in back:
            sys.stderr.write("peer got reply: %r\n" % back)
            sys.stderr.flush()
    except BlockingIOError:
        pass
    except OSError:
        break
    time.sleep(0.02)
