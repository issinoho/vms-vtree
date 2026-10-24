/* vt_view.c - the file viewer, the Info page and the Help pages. Read-only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vt_app.h"
#include "vt_util.h"

#define REC_MAX 32768

static char rec[REC_MAX];
static long cur;                 /* line number of the next record vtp_view_next gives */

void vt_view_close(vt_app *a)
{
    vt_viewer *w = &a->view;
    int i;
    if (w->v) vtp_view_close(w->v);
    for (i = 0; i < w->nlines; i++) free(w->lines[i]);
    free(w->lines);
    free(w->marks);
    {
        char find[VT_NAME_MAX];
        vt_strlcpy(find, w->find, sizeof find);       /* keep the last search */
        memset(w, 0, sizeof *w);
        vt_strlcpy(w->find, find, sizeof w->find);
    }
}

static unsigned long long file_bytes(const vt_attrs *at)
{
    if (at->eof_blocks == 0) return 0;
    if (at->ffb == 0) return (unsigned long long) at->eof_blocks * 512u;
    return (unsigned long long) (at->eof_blocks - 1) * 512u + at->ffb;
}

static int add_mark(vt_viewer *w, const vtp_pos *p)
{
    if (w->nmarks == w->capmarks) {
        long cap = w->capmarks ? w->capmarks * 2 : 256;
        vtp_pos *m = (vtp_pos *) realloc(w->marks, (size_t) cap * sizeof *m);
        if (!m) return 0;
        w->marks = m; w->capmarks = cap;
    }
    w->marks[w->nmarks++] = *p;
    return 1;
}

/* The next record into rec; 0 at the end or on an error (message set). */
static int next_rec(vt_app *a, int *len)
{
    vt_viewer *w = &a->view;
    vtp_pos pos;
    unsigned int st = vtp_view_next(w->v, rec, REC_MAX - 1, len, &pos);
    if (!(st & 1)) {
        if (!vtp_is_eof(st)) vt_status_msg(a, "Read", st);
        w->at_end = 1;
        if (cur > w->known) w->known = cur;
        return 0;
    }
    if (*len > REC_MAX - 1) *len = REC_MAX - 1;
    rec[*len] = '\0';
    if (cur % VT_VIEW_STEP == 0 && cur / VT_VIEW_STEP == w->nmarks) add_mark(w, &pos);
    cur++;
    if (cur > w->known) w->known = cur;
    return 1;
}

/* Position so that the next record is line n (or as near as the file allows). */
static void go_line(vt_app *a, long n)
{
    vt_viewer *w = &a->view;
    long k = n / VT_VIEW_STEP;
    int len;
    if (w->nmarks == 0) return;
    if (k >= w->nmarks) k = w->nmarks - 1;
    vtp_view_seek(w->v, &w->marks[k]);
    cur = k * VT_VIEW_STEP;
    while (cur < n && next_rec(a, &len)) ;
}

void vt_view_open(vt_app *a, const vt_ref *r)
{
    vt_viewer *w = &a->view;
    vt_file *f = vt_ref_file(a, r);
    unsigned int st;
    int len;
    vt_view_close(a);
    vt_ref_path(a, r, w->path, sizeof w->path);
    w->a = f->a;
    w->hex = f->a.rfm == VT_RFM_UDF || (f->a.rfm == VT_RFM_FIX && f->a.mrs == 512);
    w->size = file_bytes(&f->a);
    st = vtp_view_open(w->path, w->hex, &w->v);
    if (!(st & 1) && !w->hex) {                      /* no records: try blocks */
        w->hex = 1;
        st = vtp_view_open(w->path, 1, &w->v);
    }
    if (!(st & 1)) { vt_status_msg(a, w->path, st); w->v = 0; return; }
    if (!w->hex) {
        cur = 0;
        if (next_rec(a, &len)) { vtp_view_seek(w->v, &w->marks[0]); cur = 0; }
    }
    a->back_mode = a->mode;
    a->mode = M_VIEW;
}

static void switch_mode(vt_app *a)
{
    vt_viewer *w = &a->view;
    vtp_view *nv = 0;
    unsigned int st = vtp_view_open(w->path, !w->hex, &nv);
    int len;
    if (!(st & 1)) { vt_status_msg(a, w->hex ? "Text view" : "Hex view", st); return; }
    vtp_view_close(w->v);
    w->v = nv;
    w->hex = !w->hex;
    w->top = 0; w->hoff = 0;
    w->nmarks = 0; w->known = 0; w->at_end = 0;
    if (!w->hex) {
        cur = 0;
        if (next_rec(a, &len)) { vtp_view_seek(w->v, &w->marks[0]); cur = 0; }
    }
}

static long body_rows(vt_app *a) { return a->scr.rows - 5; }

static long hex_rows(const vt_viewer *w) { return (long) ((w->size + 15) / 16); }

static void to_bottom(vt_app *a)
{
    vt_viewer *w = &a->view;
    long h = body_rows(a), n = 0;
    int len;
    if (w->hex) { w->top = hex_rows(w) > h ? hex_rows(w) - h : 0; return; }
    go_line(a, w->known);
    while (!w->at_end && next_rec(a, &len)) {
        if (++n % 5000 == 0) {
            char m[80];
            sprintf(m, "Reading: line %ld   (any key stops)", cur);
            vt_progress(a, m);
            if (vt_check_stop(a)) { a->pushback = VK_NONE; break; }
        }
    }
    w->top = w->known > h ? w->known - h : 0;
}

static void find_next(vt_app *a)
{
    vt_viewer *w = &a->view;
    long n = 0, start;
    int len;
    if (!w->find[0]) { vt_set_msg(a, 1, "Nothing to find: press Find first"); return; }
    if (w->hex) { vt_set_msg(a, 1, "Find works in text view (H switches)"); return; }
    start = w->top + 1;
    go_line(a, start);
    if (cur != start) { vt_set_msg(a, 1, "\"%s\" not found", w->find); return; }
    while (next_rec(a, &len)) {
        if (vt_strcasestr(rec, w->find)) { w->top = cur - 1; return; }
        if (++n % 5000 == 0) {
            char m[80];
            sprintf(m, "Finding: line %ld   (any key stops)", cur);
            vt_progress(a, m);
            if (vt_check_stop(a)) { a->pushback = VK_NONE; vt_set_msg(a, 1, "Stopped"); return; }
        }
    }
    vt_set_msg(a, 1, "\"%s\" not found below line %ld", w->find, start);
}

void vt_view_key(vt_app *a, int key)
{
    vt_viewer *w = &a->view;
    long h = body_rows(a), last;
    int gold = a->gold;
    a->gold = 0;
    if (!w->v) { a->mode = a->back_mode; return; }
    last = w->hex ? hex_rows(w) - 1 : (w->at_end ? w->known - 1 : 0x7fffffffL);
    switch (key) {
    case VK_PF1: a->gold = !gold; return;
    case VK_UP: if (gold) w->top = 0; else if (w->top > 0) w->top--; break;
    case VK_DOWN: if (gold) to_bottom(a); else if (w->top < last) w->top++; break;
    case VK_PREV: w->top = w->top > h ? w->top - h : 0; break;
    case VK_NEXT: if (w->top + h <= last) w->top += h; break;
    case VK_LEFT: if (w->hoff >= 8) w->hoff -= 8; else w->hoff = 0; break;
    case VK_RIGHT: if (!w->hex) w->hoff += 8; break;
    case 'H': case 'h': switch_mode(a); break;
    case VK_FIND: a->prompt = P_FIND; vt_strlcpy(a->prompt_label, "Find: ", sizeof a->prompt_label);
        vt_edit_init(&a->edit, w->find); break;
    case VK_PF3:
        if (gold) { a->prompt = P_FIND; vt_strlcpy(a->prompt_label, "Find: ", sizeof a->prompt_label);
                    vt_edit_init(&a->edit, w->find); }
        else find_next(a);
        break;
    case VK_F10: case VK_CTRL_Z: case 'Q': case 'q': case VK_RETURN:
        vt_view_close(a);
        a->mode = a->back_mode;
        break;
    default: break;
    }
    if (a->msg_progress) { a->msg[0] = '\0'; a->msg_progress = 0; }
}

/* Draw the viewer screen. */
void vt_view_fill(vt_app *a, int height, int width)
{
    vt_viewer *w = &a->view;
    vt_screen *s = &a->scr;
    long h = body_rows(a), i;
    char where[64], line[512];
    (void) height; (void) width;

    vt_box(s, 1, 0, s->rows - 3, s->cols - 1);
    if (w->hex) {
        unsigned char blk[512];
        unsigned int have_vbn = 0;
        int blen = 0;
        if (w->top > hex_rows(w) - 1) w->top = hex_rows(w) > 0 ? hex_rows(w) - 1 : 0;
        for (i = 0; i < h && w->top + i < hex_rows(w); i++) {
            long row = w->top + i;
            unsigned int vbn = (unsigned int) (row / 32) + 1;
            int off = (int) (row % 32) * 16, j, o;
            if (vbn != have_vbn) {
                if (!(vtp_view_block(w->v, vbn, blk, &blen) & 1)) blen = 0;
                have_vbn = vbn;
            }
            o = sprintf(line, "%08lX  ", (unsigned long) row * 16);
            for (j = 0; j < 16; j++) {
                if (off + j < blen && (unsigned long long) row * 16 + j < w->size)
                    o += sprintf(line + o, "%02X ", blk[off + j]);
                else o += sprintf(line + o, "   ");
                if (j == 7) line[o++] = ' ';
            }
            line[o++] = ' ';
            for (j = 0; j < 16 && off + j < blen && (unsigned long long) row * 16 + j < w->size; j++) {
                unsigned char c = blk[off + j];
                line[o++] = (char) ((c >= 32 && c < 127) || c >= 160 ? c : '.');
            }
            line[o] = '\0';
            vt_put(s, 2 + (int) i, 2, line, 0, s->cols - 4);
        }
        snprintf(where, sizeof where, "Hex  byte %lu of %llu", (unsigned long) w->top * 16, w->size);
    } else {
        int len;
        go_line(a, w->top);
        if (cur < w->top) {                           /* past the end: back up */
            w->top = cur > h ? cur - h : 0;
            go_line(a, w->top);
        }
        for (i = 0; i < h && next_rec(a, &len); i++) {
            int c, o = 0, col = 0;
            for (c = 0; c < len && o < (int) sizeof line - 1; c++) {
                unsigned char ch = (unsigned char) rec[c];
                if (ch == '\t') {
                    do { if (col >= w->hoff && o < (int) sizeof line - 1) line[o++] = ' '; col++; } while (col % 8);
                    continue;
                }
                if (!((ch >= 32 && ch < 127) || ch >= 160)) ch = '.';
                if (col >= w->hoff) line[o++] = (char) ch;
                col++;
            }
            line[o] = '\0';
            vt_put(s, 2 + (int) i, 2, line, 0, s->cols - 4);
        }
        if (w->at_end) snprintf(where, sizeof where, "Line %ld of %ld", w->top + 1, w->known);
        else snprintf(where, sizeof where, "Line %ld", w->top + 1);
    }
    /* The position stays in view: it goes left of the clock, over the path's end. */
    vt_draw_title(a, w->path);
    {
        int wl = (int) strlen(where), at = s->cols - 17 - 4 - wl;
        if (at > 10) {
            vt_role(s, VT_R_TITLE);
            vt_fill(s, 0, at - 2, 2, ' ', VT_A_REV);
            vt_put(s, 0, at, where, VT_A_REV, -1);
            vt_role(s, VT_R_AUTO);
        }
    }
    if (a->gold)
        vt_draw_bottom(a, "GOLD", "PF3 &Find   Up top   Down bottom", "PF1 cancel GOLD");
    else
        vt_draw_bottom(a, w->hex ? "HEX" : "VIEW", w->hex ? "&H text   Prev/Next page"
                       : "Find  PF3 next  &H hex  Left/Right scroll  Prev/Next page",
                       "PF1 GOLD   GOLD Up/Down top/bottom   F10 back");
}

/* ---------- Info ---------- */

static void kv(vt_app *a, const char *k, const char *v)
{
    char line[VT_SPEC_MAX + 32];
    snprintf(line, sizeof line, "%-18s %s", k, v);
    vt_text_add(a, line);
}

void vt_info_build(vt_app *a, const vt_ref *r)
{
    vt_file *f = vt_ref_file(a, r);
    const vt_attrs *at = &f->a;
    char path[VT_SPEC_MAX], b[VT_SPEC_MAX], u[32], al[32], t[64];
    vtp_security sec;
    unsigned int st;
    int i;

    vt_text_free(a);
    vt_ref_path(a, r, path, sizeof path);
    kv(a, "File", path);
    /* (number, sequence, volume): the third word's high byte extends the number */
    snprintf(b, sizeof b, "(%lu,%u,%u)",
             (unsigned long) f->fid[0] + ((unsigned long) (f->fid[2] >> 8) << 16),
             f->fid[1], f->fid[2] & 0xFFu);
    kv(a, "File ID", b);
    if (!(at->status & 1)) {
        vtp_message(at->status, b, sizeof b);
        kv(a, "Attributes", b);
        a->itop = 0;
        return;
    }
    snprintf(b, sizeof b, "%s blocks used, %s allocated", vt_commas(at->eof_blocks, u, sizeof u),
             vt_commas(at->alloc_blocks, al, sizeof al));
    kv(a, "Size", b);
    st = vtp_security_get(path, &sec);
    vt_fmt_uic(at->uic, u, sizeof u);
    vtp_uic_name(at->uic, t, sizeof t);
    if (t[0]) snprintf(b, sizeof b, "[%s]  %s", t, u); else snprintf(b, sizeof b, "%s", u);
    kv(a, "Owner", b);
    kv(a, "Created", vt_fmt_time(at->cdt, 1, b, sizeof b));
    vt_fmt_time(at->rdt, 1, t, sizeof t);
    snprintf(b, sizeof b, "%s (%u revision%s)", t, at->revisions, at->revisions == 1 ? "" : "s");
    kv(a, "Revised", b);
    kv(a, "Expires", at->edt ? vt_fmt_time(at->edt, 1, b, sizeof b) : "<None specified>");
    kv(a, "Backup", at->bdt ? vt_fmt_time(at->bdt, 1, b, sizeof b) : "<No backup recorded>");
    kv(a, "Organization", vt_org_name(at->org));
    if (at->rfm == VT_RFM_VFC)
        snprintf(b, sizeof b, "VFC, %u byte header, maximum %u bytes, longest %u bytes",
                 at->vfc, at->mrs, at->lrl);
    else if (at->rfm == VT_RFM_FIX)
        snprintf(b, sizeof b, "%s, %u byte records", vt_rfm_name(at->rfm), at->mrs);
    else if (at->rfm == VT_RFM_VAR)
        snprintf(b, sizeof b, "%s, maximum %u bytes, longest %u bytes", vt_rfm_name(at->rfm),
                 at->mrs, at->lrl);
    else snprintf(b, sizeof b, "%s", vt_rfm_name(at->rfm));
    kv(a, "Record format", b);
    kv(a, "Record attributes", vt_fmt_rat(at->rat, b, sizeof b));
    snprintf(b, sizeof b, "Allocation %u, extend %u", at->alloc_blocks, at->defext);
    kv(a, "File attributes", b);
    if (f->flags & VT_F_DIR) {
        if (at->versions) snprintf(b, sizeof b, "%u", at->versions); else vt_strlcpy(b, "None", sizeof b);
        kv(a, "Default ver. limit", b);
    }
    {
        /* FCH$ bits (FH2 file characteristics) */
        static const struct { int bit; const char *name; } ch[] = {
            { 1, "No backup" }, { 5, "Contiguous best try" }, { 6, "Locked" },
            { 7, "Contiguous" }, { 11, "Bad ACL" }, { 12, "Spool" }, { 13, "Directory" },
            { 14, "Bad block" }, { 15, "Marked for delete" }, { 17, "Erase on delete" } };
        b[0] = '\0';
        for (i = 0; i < (int) (sizeof ch / sizeof ch[0]); i++)
            if (at->uchar & (1u << ch[i].bit)) {
                if (b[0]) vt_strlcat(b, ", ", sizeof b);
                vt_strlcat(b, ch[i].name, sizeof b);
            }
        kv(a, "Characteristics", b[0] ? b : "None");
    }
    kv(a, "File protection", vt_fmt_prot(at->prot, b, sizeof b));
    if (!(st & 1)) {
        vtp_message(st, b, sizeof b);
        kv(a, "Access Cntrl List", b);
    } else if (!(sec.acl_status & 1)) {
        vtp_message(sec.acl_status, b, sizeof b);
        kv(a, "Access Cntrl List", b);
    } else if (sec.acl_count == 0) kv(a, "Access Cntrl List", "None");
    else for (i = 0; i < sec.acl_count; i++) kv(a, i ? "" : "Access Cntrl List", sec.acl[i]);
    if (f->flags & VT_F_ALIAS) {
        vt_text_add(a, "");
        vt_text_add(a, "This directory entry is an alias: the directory's own entry is elsewhere");
        vt_text_add(a, "(its back-link is not this directory). VTREE never descends into it in");
        vt_text_add(a, "a branch-wide operation.");
    }
    a->itop = 0;
}

/* ---------- Help ---------- */

static const char *const help_tree[] = {
    "THE DIRECTORY TREE",
    "",
    "  Up/Down, Prev/Next    move;   GOLD Up/Down  top/bottom",
    "  Left / Right          parent / first subdirectory",
    "  +   -   *             open one level / close / open the whole branch",
    "                        (a + after a name: closed, with subdirectories)",
    "  Return                the file window for this directory",
    "  T  U  Select          tag / untag the files of this directory",
    "  GOLD T  GOLD U        tag / untag the files of the whole branch",
    "  B                     Branch: the files of this directory and below",
    "  S                     Showall: every directory read so far on this disk",
    "  GOLD L                Log the whole disk (read every directory)",
    "  L                     Log another disk or directory",
    "  A                     Avail: the mounted disks",
    "  F  Find               filespec:  *.COM   *.C;*,*.H   LOGIN.COM;0",
    "  ;                     all versions / highest versions only",
    "  GOLD S                sort by name, type, size or date",
    "  I                     Info: details of this directory file",
    "  M                     Make a directory here       R  Rename this one",
    "  X                     eXecute: a DCL subprocess (LOGOUT comes back)",
    "  GOLD W                read this directory again",
    "  Ctrl/W                redraw the screen",
    "  F10  Ctrl/Z           exit;   Q  quit (asks first)",
    "",
    "Directories are read as you open them, so a large disk starts at once.",
    "An entry marked (alias) is another name for a directory elsewhere, such",
    "as SYSCOMMON on a system disk: branch-wide commands never go into it.",
    "(unchecked): its entry could not be read, so it might be an alias; it is",
    "treated as one. Open it by hand, or start the command inside it.",
    "A ? after a name: the directory could not be read (I tells you why).",
    "",
    "VTREE switches off BYPASS, SYSPRV, READALL and GRPPRV while it runs, so",
    "it can only see and do what file protection allows you.",
    0 };

static const char *const help_files[] = {
    "THE FILE WINDOW",
    "",
    "  Arrows, Prev/Next     move;   GOLD Up/Down  first/last file",
    "  T  U  Select          tag / untag / toggle, then move on",
    "  GOLD T  GOLD U        tag / untag every file listed",
    "  V                     view the file (text by records, or hex)",
    "  I                     Info: sizes, dates, owner, protection, ACL",
    "  F  Find               filespec;   ;  versions;   =  one line per file",
    "  GOLD S                sort by name, type, size or date",
    "  C  M  R               Copy, Move (same disk), Rename this file",
    "  GOLD C  GOLD M  GOLD R  the same for every tagged file",
    "  E                     Edit (VTREE$EDITOR, default EDIT/TPU)",
    "  X                     eXecute: a DCL subprocess (LOGOUT comes back)",
    "  GOLD W                read this file's directory again",
    "  Return  F10           back to the tree",
    "",
    "Copy keeps what COPY keeps; the copy gets your default protection. At the",
    "\"to:\" prompt, Select or Tab lets you point at the directory in the tree.",
    "Nothing is ever overwritten: a copy is a new version, and a copy or move that",
    "would make RMS delete old versions (a version limit) is refused. Each change",
    "goes to the log, SYS$LOGIN:VTREE.LOG (or VTREE$LOG).",
    "",
    "A diamond marks a tagged file. Directory files are shown in the tree.",
    "",
    "IN THE VIEWER",
    "",
    "  Up/Down, Prev/Next    scroll;   GOLD Up/Down  top/bottom",
    "  Left/Right            scroll sideways",
    "  Find  GOLD PF3        find text;   PF3  find the next one",
    "  H                     hex (blocks) / text (records)",
    "  F10  Return           back",
    0 };

void vt_help_build(vt_app *a, int for_mode)
{
    const char *const *h = for_mode == M_FILES ? help_files : help_tree;
    char line[80];
    vt_text_free(a);
    for (; *h; h++) vt_text_add(a, *h);
    vt_text_add(a, "");
    snprintf(line, sizeof line, "VTREE V%s - inspired by XTree Gold. MIT licence.", VT_VERSION);
    vt_text_add(a, line);
}
