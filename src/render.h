/*
 * render.h - paint a VT100 screen model onto the local CP/M console
 *
 * Targets the Amstrad PCW CP/M Plus console, which is a VT52-style terminal:
 *   ESC Y <row+32> <col+32>   cursor position (row first)
 *   ESC E                     clear screen + home
 *   ESC p / ESC q             inverse video on / off
 *   ESC r / ESC u             bright on / off
 *
 * render_flush() diffs the model against a shadow buffer and writes only the
 * cells that changed, so a screenful of VT100 traffic costs only as much
 * console output as actually moved.
 */
#ifndef VTERM_RENDER_H
#define VTERM_RENDER_H

#include "vt100.h"

void render_init(void);        /* clear the console and reset the shadow   */
void render_flush(VT *t);      /* paint changed cells, then place the cursor */

#endif /* VTERM_RENDER_H */
