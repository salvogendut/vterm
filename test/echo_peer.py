#!/usr/bin/env python3
"""Minimal serial echo peer for the vterm passthrough test.

Opens the emulator's serial PTY in raw mode and echoes every byte it
receives back to the PCW. With vterm running in passthrough mode (which does
NOT echo locally), characters typed on the PCW travel out the serial line,
get echoed here, return over serial, and appear on screen -- exercising both
the TX and RX paths of the CPS8256 backend in a single round trip.

Usage: echo_peer.py <pty-path> [duration-seconds]
"""
import os, sys, time, termios

path = sys.argv[1]
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0

fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
try:                                   # raw line discipline
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0       # iflag, oflag, lflag = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
except Exception:
    pass
os.set_blocking(fd, False)

deadline = time.time() + duration
while time.time() < deadline:
    try:
        d = os.read(fd, 256)
    except BlockingIOError:
        d = b""
    except OSError:
        break
    if d:
        try:
            os.write(fd, d)            # echo back to the PCW
        except OSError:
            break
    else:
        time.sleep(0.005)
