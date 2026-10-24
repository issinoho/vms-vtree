/* vt_ansi.h - turn the screen model into VT100/VT220 sequences (D11).

   Colour mode uses the XTree Gold palette (blue screen, white text, yellow keys
   and labels, cyan frames, a cyan highlight bar) through ANSI SGR colours, which
   DECterm, xterm and PuTTY show and a real VT220/VT320 ignores. Monochrome mode
   uses bold, underline and reverse only, as SMG$ would. Line drawing uses the DEC
   Special Graphics set (ESC ( 0). Only cells that differ from prev are sent. */
#ifndef VT_ANSI_H
#define VT_ANSI_H

#include "vt_screen.h"

typedef void (*vt_emit_fn)(void *ctx, const char *bytes, int len);

/* Send cur. prev is what the terminal shows now (cur's size), or 0 for a full
   repaint; it is updated to cur. */
void vt_ansi_render(const vt_screen *cur, vt_cell *prev, int colour, vt_emit_fn emit, void *ctx);
/* Before the first frame (colour background, cursor off) and at exit (normal
   rendition, ASCII, cursor on, screen cleared). */
void vt_ansi_begin(int colour, vt_emit_fn emit, void *ctx);
void vt_ansi_end(vt_emit_fn emit, void *ctx);
/* The SGR parameters a cell gets, e.g. "0;36;40;7". For tests. */
const char *vt_ansi_sgr(const vt_cell *c, int colour);

#endif
