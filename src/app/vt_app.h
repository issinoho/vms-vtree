/* vt_app.h - VTREE's state and behaviour. Portable: it reaches the OS only through
   vt_platform.h, so it runs unchanged on VMS and against the Linux stub. */
#ifndef VT_APP_H
#define VT_APP_H

#include "vt_types.h"
#include "vt_tree.h"
#include "vt_match.h"
#include "vt_edit.h"
#include "vt_screen.h"
#include "vt_platform.h"

enum { M_TREE, M_FILES, M_VIEW, M_INFO, M_HELP, M_AVAIL };
enum { L_DIR, L_BRANCH, L_SHOWALL };
enum { S_NAME, S_TYPE, S_SIZE, S_DATE };
enum { P_NONE, P_FILESPEC, P_LOG, P_FIND, P_SORT, P_QUIT };

typedef struct { int node, idx; } vt_ref;    /* a file: t.n[node].files.f[idx] */

#define VT_VIEW_STEP 32       /* a remembered position every 32 lines */

typedef struct {
    vtp_view *v;
    int hex;
    char path[VT_SPEC_MAX];
    vt_attrs a;
    long top;                 /* first line shown (text) or 16-byte row (hex) */
    int hoff;                 /* text: columns scrolled left */
    vtp_pos *marks;           /* marks[k]: where line k*VT_VIEW_STEP starts */
    long nmarks, capmarks;
    long known;               /* lines counted so far */
    int at_end;               /* known is the whole file */
    unsigned long long size;  /* bytes, for hex */
    char find[VT_NAME_MAX];
    char **lines;             /* the lines on screen, filled by vt_view_fill */
    int nlines;
} vt_viewer;

typedef struct {
    vt_tree t;
    vtp_session sess;
    vt_screen scr;
    int rows, cols;
    int mode, back_mode;
    int gold;
    int quit;

    /* tree window */
    int *vis; int nvis, capvis;
    int tcur, ttop;           /* index into vis */

    /* file window */
    vt_filter filter;
    int highest_only, sort, sort_rev, fmt;
    int kind, list_node;      /* what the file window lists */
    vt_ref *refs; int nrefs, caprefs;
    int fcur, ftop;
    int refs_dirty;
    long all_files; unsigned long all_blocks;      /* the list, before the filter */
    long match_blocks;
    int fcols, frows;         /* layout of the last draw */

    int pending_load;         /* current directory's files still to be read */
    int pushback;             /* a key read while working, to be handled next */

    /* prompt */
    int prompt; char prompt_label[48]; vt_edit edit;

    char msg[200]; int msg_err; int msg_progress;

    vt_viewer view;
    char **info; int ninfo; int itop;              /* Info and Help text */
    vtp_device devs[64]; int ndevs, dcur;

    int progress_count;
} vt_app;

/* Set up and log the start directory (spec, or the default directory if 0). */
int  vt_app_init(vt_app *a, const char *spec, int rows, int cols);
void vt_app_free(vt_app *a);
void vt_app_draw(vt_app *a);           /* fill a->scr */
void vt_app_key(vt_app *a, int key);
int  vt_app_idle(vt_app *a);           /* deferred work; 1 if something was done */
void vt_app_resize(vt_app *a, int rows, int cols);
void vt_app_run(vt_app *a);            /* the main loop (draw, key, idle) */

/* internals shared by vt_app.c, vt_draw.c and vt_view.c */
int  vt_cur_node(vt_app *a);
vt_file *vt_ref_file(vt_app *a, const vt_ref *r);
void vt_set_msg(vt_app *a, int err, const char *fmt, ...);
void vt_status_msg(vt_app *a, const char *what, unsigned int st);
void vt_rebuild_refs(vt_app *a);
void vt_ref_path(vt_app *a, const vt_ref *r, char *out, int size);
int  vt_check_stop(vt_app *a);         /* a key is waiting: keep it, return 1 */
void vt_progress(vt_app *a, const char *text);
void vt_text_free(vt_app *a);
void vt_text_add(vt_app *a, const char *line);

void vt_view_open(vt_app *a, const vt_ref *r);
void vt_view_close(vt_app *a);
void vt_view_key(vt_app *a, int key);
void vt_view_fill(vt_app *a, int height, int width);
void vt_info_build(vt_app *a, const vt_ref *r);
void vt_help_build(vt_app *a, int for_mode);
void vt_draw_title(vt_app *a, const char *what);
void vt_draw_bottom(vt_app *a, const char *l1label, const char *l1, const char *l2);

#endif
