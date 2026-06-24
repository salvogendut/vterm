#!/usr/bin/env python3
"""Bridge the emulator's serial PTY to an interactive shell.

With this running, vterm becomes a VT100 terminal into a real shell: what you
type in vterm goes to the shell, and the shell's VT100 output is rendered by
vterm. The shell is told it's a vt100 in an 80x24 window.

Usage: serial_shell.py <pty-path> [command...]   (default command: bash -i)
"""
import os, sys, pty, select, fcntl, struct, termios

link = sys.argv[1]
cmd = sys.argv[2:] or ["bash", "-i"]

ser = os.open(link, os.O_RDWR | os.O_NOCTTY)
a = termios.tcgetattr(ser)            # raw on the serial side
a[0] = a[1] = a[3] = 0
termios.tcsetattr(ser, termios.TCSANOW, a)

pid, master = pty.fork()
if pid == 0:                          # child: the shell
    os.environ["TERM"] = "vt100"
    os.environ["COLUMNS"] = "80"
    os.environ["LINES"] = "24"
    os.environ["PS1"] = "$ "
    os.execvp(cmd[0], cmd)
    os._exit(1)

# tell the shell its window is 80x24
fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))

while True:
    r, _, _ = select.select([ser, master], [], [])
    if ser in r:
        d = os.read(ser, 256)
        if not d:
            break
        os.write(master, d)
    if master in r:
        try:
            d = os.read(master, 256)
        except OSError:
            break
        if not d:
            break
        os.write(ser, d)
