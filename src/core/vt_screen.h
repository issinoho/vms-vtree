/* vt_screen.h - the screen model: a grid of cells, each a character, a rendition
   and a character set. The app draws into it; a platform renders it (SMG$ on VMS,
   text for the tests). */
#ifndef VT_SCREEN_H
#define VT_SCREEN_H

#define VT_A_BOLD  1
#define VT_A_REV   2
#define VT_A_UNDER 4
#define VT_A_GFX   8      /* ch is a DEC Special Graphics character */

/* DEC Special Graphics characters (shown as these ASCII codes with VT_A_GFX). */
#define VT_G_DIAMOND '`'
#define VT_G_HLINE   'q'
#define VT_G_VLINE   'x'
#define VT_G_TL      'l'
#define VT_G_TR      'k'
#define VT_G_BL      'm'
#define VT_G_BR      'j'
#define VT_G_LTEE    't'   /* |- */
#define VT_G_RTEE    'u'   /* -| */
#define VT_G_TTEE    'w'
#define VT_G_BTEE    'v'
#define VT_G_CROSS   'n'

typedef struct { unsigned char ch, attr; } vt_cell;

typedef struct {
    int rows, cols;
    vt_cell *cell;           /* rows * cols */
    int cursor_row, cursor_col, cursor_on;
} vt_screen;

int  vt_screen_init(vt_screen *s, int rows, int cols);   /* 0 on no memory */
void vt_screen_free(vt_screen *s);
void vt_screen_clear(vt_screen *s);
vt_cell *vt_at(vt_screen *s, int row, int col);           /* 0 when outside */

/* Text at (row, col), cut at max columns (max < 0: to the edge). Control
   characters show as '?'. Returns the columns written. */
int  vt_put(vt_screen *s, int row, int col, const char *text, unsigned char attr, int max);
/* Fill count cells with ch. */
void vt_fill(vt_screen *s, int row, int col, int count, unsigned char ch, unsigned char attr);
void vt_gfx(vt_screen *s, int row, int col, unsigned char g, unsigned char attr);
/* Box with corners at (r1,c1) and (r2,c2). */
void vt_box(vt_screen *s, int r1, int c1, int r2, int c2);
void vt_hline(vt_screen *s, int row, int c1, int c2);
void vt_vline(vt_screen *s, int col, int r1, int r2);
/* Set attributes on a run of cells (e.g. the highlight bar). */
void vt_attr_run(vt_screen *s, int row, int col, int count, unsigned char attr);

/* The grid as UTF-8 text, one line per row, trailing blanks kept off; DEC graphics
   become box-drawing characters. For snapshot tests. Returns bytes written. */
int  vt_screen_text(const vt_screen *s, char *out, int size);

#endif
