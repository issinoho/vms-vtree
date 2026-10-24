/* vt_ansi.c - screen model to VT sequences. No I/O: bytes go to a callback. */
#include <stdio.h>
#include <string.h>
#include "vt_ansi.h"

/* The palette (after XTree Gold's defaults). Bright = bold on DEC-style terminals. */
#define P_TEXT    "0;37;44"     /* white on blue */
#define P_BRIGHT  "0;1;37;44"   /* bright white on blue */
#define P_KEY     "0;1;33;44"   /* yellow on blue: keys, labels, tags */
#define P_FRAME   "0;36;44"     /* cyan lines */
#define P_LABEL   "0;1;36;44"   /* bright cyan: DIR, COMMANDS */
#define P_BAR     "0;30;46"     /* black on cyan: the highlight bar */
#define P_BAR_OFF "0;30;47"     /* black on white: the bar of the window not in use */
#define P_ERROR   "0;1;37;41"   /* bright white on red */
#define P_UNDER   "0;4;37;44"

const char *vt_ansi_sgr(const vt_cell *c, int colour)
{
    unsigned char a = c->attr;
    if (!colour) {
        static char m[16];
        strcpy(m, "0");
        if (a & VT_A_BOLD) strcat(m, ";1");
        if (a & VT_A_UNDER) strcat(m, ";4");
        if (a & VT_A_REV) strcat(m, ";7");
        return m;
    }
    switch (c->role) {
    case VT_R_TITLE: return (a & VT_A_BOLD) ? P_KEY : P_BRIGHT;
    case VT_R_MENU: return (a & VT_A_BOLD) ? P_KEY : P_TEXT;
    case VT_R_MENU_LABEL: return P_LABEL;
    default: break;
    }
    if ((a & VT_A_BOLD) && (a & VT_A_REV)) return P_ERROR;
    if ((a & VT_A_REV) && (a & VT_A_UNDER)) return P_BAR_OFF;
    if (a & VT_A_REV) return P_BAR;
    if (a & VT_A_GFX) return c->ch == VT_G_DIAMOND ? P_KEY : P_FRAME;
    if (a & VT_A_BOLD) return P_KEY;
    if (a & VT_A_UNDER) return P_UNDER;
    return P_TEXT;
}

typedef struct {
    vt_emit_fn emit; void *ctx;
    char buf[4096]; int n;
    char sgr[24]; int gfx;
} out_t;

static void flush(out_t *o) { if (o->n) o->emit(o->ctx, o->buf, o->n); o->n = 0; }

static void put(out_t *o, const char *s, int len)
{
    if (o->n + len > (int) sizeof o->buf) flush(o);
    memcpy(o->buf + o->n, s, (size_t) len);
    o->n += len;
}

static void puts_(out_t *o, const char *s) { put(o, s, (int) strlen(s)); }

static void cell_out(out_t *o, const vt_cell *c, int colour)
{
    const char *sgr = vt_ansi_sgr(c, colour);
    int gfx = (c->attr & VT_A_GFX) != 0;
    char ch = (char) c->ch;
    if (strcmp(sgr, o->sgr)) {
        char m[48];
        sprintf(m, "\033[%.24sm", sgr);
        puts_(o, m);
        strcpy(o->sgr, sgr);
    }
    if (gfx != o->gfx) { puts_(o, gfx ? "\033(0" : "\033(B"); o->gfx = gfx; }
    put(o, &ch, 1);
}

void vt_ansi_render(const vt_screen *cur, vt_cell *prev, int colour, vt_emit_fn emit, void *ctx)
{
    static out_t o;
    int r, c;
    o.emit = emit; o.ctx = ctx; o.n = 0; o.sgr[0] = '\0'; o.gfx = 0;   /* every frame starts and ends in ASCII */
    puts_(&o, "\033[?25l");                         /* cursor off while drawing */
    for (r = 0; r < cur->rows; r++) {
        int at_col = -1;                             /* where the terminal cursor is */
        for (c = 0; c < cur->cols; c++) {
            const vt_cell *n = &cur->cell[r * cur->cols + c];
            vt_cell *p = prev ? &prev[r * cur->cols + c] : 0;
            if (p && p->ch == n->ch && p->attr == n->attr && p->role == n->role) continue;
            if (at_col != c) {
                char m[48];
                sprintf(m, "\033[%d;%dH", r + 1, c + 1);
                puts_(&o, m);
            }
            cell_out(&o, n, colour);
            at_col = c + 1;
            if (p) *p = *n;
        }
    }
    if (o.gfx == 1) puts_(&o, "\033(B");
    if (cur->cursor_on) {
        char m[64];
        sprintf(m, "\033[%d;%dH\033[?25h", cur->cursor_row + 1, cur->cursor_col + 1);
        puts_(&o, m);
    }
    flush(&o);
}

void vt_ansi_begin(int colour, vt_emit_fn emit, void *ctx)
{
    const char *s = colour ? "\033[0;37;44m\033(B\033[?25l\033[H\033[2J" : "\033[0m\033(B\033[?25l\033[H\033[2J";
    emit(ctx, s, (int) strlen(s));
}

void vt_ansi_end(vt_emit_fn emit, void *ctx)
{
    const char *s = "\033[0m\033(B\033[?25h\033[H\033[2J";
    emit(ctx, s, (int) strlen(s));
}
