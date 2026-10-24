/* vt_draw.c - draw VTREE's screens into the screen model. */
#include <stdio.h>
#include <string.h>
#include "vt_app.h"
#include "vt_util.h"

#define PANEL_W 21

/* A menu line: '&' marks the next character as a key (bold). */
static void menu_line(vt_screen *s, int row, const char *label, const char *text, unsigned char base)
{
    int col = 1, n;
    vt_fill(s, row, 0, s->cols, ' ', base);
    n = vt_put(s, row, col, label, (unsigned char) (base | VT_A_REV), -1);
    col += n + 2;
    for (; *text && col < s->cols; text++) {
        if (*text == '&' && text[1]) {
            text++;
            vt_fill(s, row, col++, 1, (unsigned char) *text, (unsigned char) (base | VT_A_BOLD));
        } else vt_fill(s, row, col++, 1, (unsigned char) *text, base);
    }
}

void vt_draw_title(vt_app *a, const char *what)
{
    vt_screen *s = &a->scr;
    char clock[32], t[VT_SPEC_MAX + 32];
    int cl;
    vt_fmt_time(vtp_now(), 0, clock, sizeof clock);
    cl = (int) strlen(clock);
    vt_fill(s, 0, 0, s->cols, ' ', VT_A_REV);
    snprintf(t, sizeof t, " VTREE   %s", what);
    vt_put(s, 0, 0, t, VT_A_REV, s->cols - cl - 3);
    vt_put(s, 0, s->cols - cl - 1, clock, VT_A_REV, -1);
}

void vt_draw_bottom(vt_app *a, const char *l1label, const char *l1, const char *l2)
{
    vt_screen *s = &a->scr;
    int r1 = s->rows - 2, r2 = s->rows - 1;
    if (a->prompt != P_NONE) {
        int n;
        vt_fill(s, r1, 0, s->cols, ' ', 0);
        n = vt_put(s, r1, 1, a->prompt_label, VT_A_BOLD, -1);
        if (a->prompt == P_SORT || a->prompt == P_QUIT) {
            s->cursor_on = 1; s->cursor_row = r1; s->cursor_col = 1 + n;
            menu_line(s, r2, "CHOOSE", "Press the letter of your choice", VT_A_REV);
            return;
        }
        {
            int width = s->cols - 2 - n, start = 0;
            if (width < 10) width = 10;
            if (a->edit.pos >= width) start = a->edit.pos - width + 1;
            vt_put(s, r1, 1 + n, a->edit.buf + start, 0, width);
            s->cursor_on = 1; s->cursor_row = r1; s->cursor_col = 1 + n + a->edit.pos - start;
        }
        menu_line(s, r2, "EDIT", "Return accept   F10 cancel   Ctrl/U erase   Ctrl/H start   Ctrl/E end", VT_A_REV);
        return;
    }
    if (a->msg[0]) {
        vt_fill(s, r1, 0, s->cols, ' ', 0);
        vt_put(s, r1, 1, a->msg, a->msg_err ? (VT_A_BOLD | VT_A_REV) : VT_A_BOLD, s->cols - 2);
    } else menu_line(s, r1, l1label, l1, 0);
    menu_line(s, r2, "COMMANDS", l2, VT_A_REV);
}

/* ---------- the tree ---------- */

static void draw_tree(vt_app *a, int r1, int r2, int c1, int c2)
{
    vt_screen *s = &a->scr;
    int rows = r2 - r1 + 1, i;
    if (a->tjump && (a->tcur < a->ttop || a->tcur >= a->ttop + rows - 2)) a->ttop = a->tcur - rows / 3;
    a->tjump = 0;
    if (a->ttop > a->nvis - rows) a->ttop = a->nvis - rows;
    if (a->tcur < a->ttop) a->ttop = a->tcur;
    if (a->tcur >= a->ttop + rows) a->ttop = a->tcur - rows + 1;
    if (a->ttop < 0) a->ttop = 0;
    for (i = 0; i < rows && a->ttop + i < a->nvis; i++) {
        int node = a->vis[a->ttop + i], row = r1 + i, d = a->t.n[node].depth, k, col, n;
        const vt_node *nd = &a->t.n[node];
        unsigned char at = 0;
        char suffix[16] = "";
        int anc[256], m = 0, p;
        for (p = nd->parent; p >= 0 && m < 256; p = a->t.n[p].parent) anc[m++] = p;
        /* anc[m-1] is the root (depth 0); anc[m-1-k] is at depth k */
        for (k = 1; k < d; k++) {
            int an = anc[m - 1 - k];
            col = c1 + 2 * (k - 1);
            if (col <= c2 && !vt_tree_is_last(&a->t, an)) vt_gfx(s, row, col, VT_G_VLINE, 0);
        }
        if (d > 0) {
            col = c1 + 2 * (d - 1);
            if (col <= c2) vt_gfx(s, row, col, vt_tree_is_last(&a->t, node) ? VT_G_BL : VT_G_LTEE, 0);
            if (col + 1 <= c2) vt_gfx(s, row, col + 1, VT_G_HLINE, 0);
        }
        col = c1 + 2 * d;
        if (nd->flags & VT_N_ALIAS) strcpy(suffix, " (alias)");
        else if (nd->flags & VT_N_NOLINK) strcpy(suffix, (nd->flags & VT_N_ERROR) ? " (unchecked) ?" : " (unchecked)");
        else if (nd->flags & VT_N_ERROR) strcpy(suffix, " ?");
        else if ((nd->flags & VT_N_SCANNED) && nd->child >= 0 && !(nd->flags & VT_N_OPEN))
            strcpy(suffix, " +");
        if (d == 0) at = VT_A_BOLD;
        if (a->ttop + i == a->tcur) at = a->mode == M_TREE ? VT_A_REV : (VT_A_REV | VT_A_UNDER);
        if (col > c2) continue;
        n = vt_put(s, row, col, nd->name, at, c2 - col + 1);
        vt_put(s, row, col + n, suffix, 0, c2 - col - n + 1);
    }
}

/* ---------- file window ---------- */

static int ref_label(vt_app *a, const vt_ref *r, char *buf, int size)
{
    vt_file *f = vt_ref_file(a, r);
    return snprintf(buf, (size_t) size, "%s%s;%u", f->name, f->type, f->ver);
}

static void draw_files(vt_app *a, int r1, int r2, int c1, int c2, int active)
{
    vt_screen *s = &a->scr;
    int width = c2 - c1 + 1, rows = r2 - r1 + 1, maxlen = 8, i, cw, cols;
    char buf[VT_SPEC_MAX];
    vt_node *nd = a->list_node >= 0 ? &a->t.n[a->list_node] : 0;

    if (a->refs_dirty) vt_rebuild_refs(a);
    if (a->nrefs == 0) {
        const char *why = "No files";
        if (a->kind == L_DIR && nd && !nd->files.loaded) why = "Reading...";
        else if (a->kind == L_DIR && nd && !(nd->files.status & 1)) {
            vtp_message(nd->files.status, buf, sizeof buf);
            why = buf;
        } else if (a->all_files) why = "No files match the filespec";
        vt_put(s, r1, c1 + 1, why, 0, width - 1);
        if (active) { a->fcols = 1; a->frows = rows; }
        return;
    }
    for (i = 0; i < a->nrefs; i++) {
        int n = ref_label(a, &a->refs[i], buf, sizeof buf);
        if (n > maxlen) maxlen = n;
    }
    if (a->fmt == 0 || !active) {
        /* One long name must not leave a single column: cap the width; a cut
           name ends in '>' and the title line shows it whole. */
        int cap = width / 3 > 22 ? width / 3 : 22;
        cw = maxlen + 3;
        if (cw > cap) cw = cap;
        if (cw > width) cw = width;
        cols = width / cw;
        if (cols < 1) cols = 1;
    } else { cw = width; cols = 1; }
    if (active) { a->fcols = cols; a->frows = rows; }
    if (!active) a->ftop = 0;
    else {
        if (a->fcur < a->ftop) a->ftop = a->fcur - a->fcur % cols;
        if (a->fcur >= a->ftop + rows * cols) a->ftop = (a->fcur / cols - rows + 1) * cols;
    }
    for (i = 0; i < rows * cols && a->ftop + i < a->nrefs; i++) {
        int idx = a->ftop + i, row = r1 + i / cols, col = c1 + (i % cols) * cw;
        vt_file *f = vt_ref_file(a, &a->refs[idx]);
        unsigned char at = (active && idx == a->fcur) ? VT_A_REV : 0;
        int n;
        if (f->flags & VT_F_TAGGED) vt_gfx(s, row, col, VT_G_DIAMOND, VT_A_BOLD);
        n = ref_label(a, &a->refs[idx], buf, sizeof buf);
        if (cols > 1 || !active || a->fmt == 0) {
            vt_put(s, row, col + 1, buf, at, cw - 2);
            if (n > cw - 2) vt_put(s, row, col + cw - 2, ">", (unsigned char) (at | VT_A_BOLD), 1);
            if (at && n < cw - 2) vt_attr_run(s, row, col + 1 + n, 0, at);
        } else {
            /* one line: name, blocks used/allocated, revised, protection */
            char line[VT_SPEC_MAX + 128], sz[40], u[16], al[16], dt[32], pr[48];
            int nw = maxlen;
            if (nw > width - 40) nw = width - 40;
            if (nw < 12) nw = 12;
            if (f->a.status & 1) {
                vt_commas(f->a.eof_blocks, u, sizeof u);
                vt_commas(f->a.alloc_blocks, al, sizeof al);
                snprintf(sz, sizeof sz, "%9s/%-9s", u, al);
                vt_fmt_time(f->a.rdt, 0, dt, sizeof dt);
                vt_fmt_prot(f->a.prot, pr, sizeof pr);
            } else {
                vtp_message(f->a.status, line, sizeof line);
                snprintf(sz, sizeof sz, "%-19s", "no attributes");
                dt[0] = '\0'; pr[0] = '\0';
            }
            snprintf(line, sizeof line, "%-*.*s %s %17s  %s", nw, nw, buf, sz, dt,
                     width >= nw + 70 ? pr : "");
            vt_put(s, row, col + 1, line, at, width - 1);
        }
    }
}

/* ---------- the statistics panel ---------- */

static void panel(vt_app *a, int r1, int r2, int c1)
{
    vt_screen *s = &a->scr;
    int r = r1, node = a->list_node >= 0 ? a->list_node : vt_cur_node(a), root, i;
    char b[64], line[64], dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    vtp_device d;
    long tf = 0; unsigned long tb = 0;
    static char last_dev[64]; static vtp_device last_d; static vt_time last_t;
    const char *what = a->kind == L_BRANCH ? "BRANCH" : a->kind == L_SHOWALL ? "SHOWALL" : "DIRECTORY";

    if (node < 0) return;
    for (root = node; a->t.n[root].parent >= 0; root = a->t.n[root].parent) ;
    vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
    /* Device figures: at most once every 10 s (a $GETDVI each draw is wasteful). */
    if (strcmp(last_dev, a->t.n[root].name) || vtp_now() - last_t > 100000000ULL) {
        memset(&last_d, 0, sizeof last_d);
        vtp_device_info(a->t.n[root].name, &last_d);
        vt_strlcpy(last_dev, a->t.n[root].name, sizeof last_dev);
        last_t = vtp_now();
    }
    d = last_d;
    for (i = 0; i < a->t.count; i++) {
        const vt_flist *fl = &a->t.n[i].files;
        int j;
        for (j = 0; j < fl->n; j++)
            if (fl->f[j].flags & VT_F_TAGGED) { tf++; tb += fl->f[j].a.eof_blocks; }
    }
    if (a->refs_dirty) vt_rebuild_refs(a);

#define PL(text, at) do { if (r <= r2) vt_put(s, r++, c1, (text), (at), PANEL_W - 2); } while (0)
    PL("FILE", VT_A_BOLD);
    line[0] = ' ';
    vt_strlcpy(line + 1, a->filter.text, sizeof line - 1);
    if (a->highest_only) vt_strlcat(line, " (hi)", sizeof line);
    PL(line, 0);
    PL("DISK", VT_A_BOLD);
    snprintf(line, sizeof line, " %s", a->t.n[root].name); PL(line, 0);
    snprintf(line, sizeof line, " %s", d.label); PL(line, 0);
    snprintf(line, sizeof line, " Free %12s", vt_commas(d.free_blocks, b, sizeof b)); PL(line, 0);
    PL(what, VT_A_BOLD);
    snprintf(line, sizeof line, " Files  %10s", vt_commas((unsigned long) a->all_files, b, sizeof b)); PL(line, 0);
    snprintf(line, sizeof line, " Blocks %10s", vt_commas(a->all_blocks, b, sizeof b)); PL(line, 0);
    PL("MATCHING", VT_A_BOLD);
    snprintf(line, sizeof line, " Files  %10s", vt_commas((unsigned long) a->nrefs, b, sizeof b)); PL(line, 0);
    snprintf(line, sizeof line, " Blocks %10s", vt_commas((unsigned long) a->match_blocks, b, sizeof b)); PL(line, 0);
    PL("TAGGED", VT_A_BOLD);
    snprintf(line, sizeof line, " Files  %10s", vt_commas((unsigned long) tf, b, sizeof b)); PL(line, 0);
    snprintf(line, sizeof line, " Blocks %10s", vt_commas(tb, b, sizeof b)); PL(line, 0);
    r++;
    snprintf(line, sizeof line, "%s %s", a->sess.user, vt_fmt_uic(a->sess.uic, b, sizeof b)); PL(line, 0);
    PL(a->sess.privs_off[0] ? " Privs off" : " No privs to drop", 0);
#undef PL
}

/* ---------- the main screen ---------- */

static void draw_main(vt_app *a)
{
    vt_screen *s = &a->scr;
    int top = 1, bot = s->rows - 3, pw = s->cols >= 60 ? PANEL_W : 0;
    int cx = pw ? s->cols - pw - 1 : s->cols - 1;
    char title[VT_SPEC_MAX], dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    int node = vt_cur_node(a);

    if (node >= 0) {
        vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
        vt_strlcpy(title, dev, sizeof title);
        vt_strlcat(title, dir, sizeof title);
    } else title[0] = '\0';
    if (a->mode == M_FILES && a->nrefs && !a->refs_dirty) {
        vt_ref_path(a, &a->refs[a->fcur], title, sizeof title);
    }
    vt_box(s, top, 0, bot, s->cols - 1);
    if (pw) {
        vt_vline(s, cx, top + 1, bot - 1);
        vt_gfx(s, top, cx, VT_G_TTEE, 0);
        vt_gfx(s, bot, cx, VT_G_BTEE, 0);
        panel(a, top + 1, bot - 1, cx + 2);
    }
    if (a->mode == M_TREE) {
        int inner = bot - top - 1, frows = inner / 3 < 3 ? 3 : inner / 3;
        int hs = bot - frows - 1;
        draw_tree(a, top + 1, hs - 1, 2, cx - 2);
        vt_hline(s, hs, 1, cx - 1);
        vt_gfx(s, hs, 0, VT_G_LTEE, 0);
        vt_gfx(s, hs, cx, pw ? VT_G_RTEE : VT_G_RTEE, 0);
        draw_files(a, hs + 1, bot - 1, 1, cx - 1, 0);
        vt_draw_title(a, title);
        if (a->gold)
            vt_draw_bottom(a, "GOLD", "Log &whole disk   &Reread   &Sort   &Tag branch   &Untag branch   Up/Down top/bottom",
                        "PF1 cancel GOLD");
        else
            vt_draw_bottom(a, "DIR", "&Avail &Branch &Filespec &Info &Log &Showall &Tag &Untag  &+ &- &*  &; versions",
                        "PF1 GOLD   Return files   Select tag   Help   F10 exit");
    } else {
        draw_files(a, top + 1, bot - 1, 1, cx - 1, 1);
        if (a->mode == M_FILES && a->nrefs) vt_ref_path(a, &a->refs[a->fcur], title, sizeof title);
        vt_draw_title(a, title);
        if (a->gold)
            vt_draw_bottom(a, "GOLD", "&Reread   &Sort   &Tag all   &Untag all   Up/Down top/bottom", "PF1 cancel GOLD");
        else
            vt_draw_bottom(a, a->kind == L_SHOWALL ? "SHOWALL" : a->kind == L_BRANCH ? "BRANCH" : "FILE",
                        "&Filespec &Info &Tag &Untag &View  &= format  &; versions",
                        "PF1 GOLD   Return tree   Select tag   Help   F10 tree");
    }
}

/* ---------- other screens ---------- */

static void draw_text(vt_app *a, const char *title, const char *label)
{
    vt_screen *s = &a->scr;
    int rows = s->rows - 4, i;
    vt_draw_title(a, title);
    vt_box(s, 1, 0, s->rows - 3, s->cols - 1);
    for (i = 0; i < rows - 1 && a->itop + i < a->ninfo; i++)
        vt_put(s, 2 + i, 2, a->info[a->itop + i], 0, s->cols - 4);
    vt_draw_bottom(a, label, "Up/Down Prev/Next scroll", "Any other key returns");
}

static void draw_avail(vt_app *a)
{
    vt_screen *s = &a->scr;
    int i;
    char line[160], f[32], m[32];
    vt_draw_title(a, "Mounted disks");
    vt_box(s, 1, 0, s->rows - 3, s->cols - 1);
    vt_put(s, 2, 2, "Disk                  Label         Free blocks    Total blocks  ODS", VT_A_BOLD, s->cols - 4);
    for (i = 0; i < a->ndevs && 3 + i < s->rows - 3; i++) {
        const vtp_device *d = &a->devs[i];
        snprintf(line, sizeof line, "%-21.21s %-12.12s %13s %15s  %d", d->name, d->label,
                 vt_commas(d->free_blocks, f, sizeof f), vt_commas(d->max_blocks, m, sizeof m), d->ods);
        vt_put(s, 3 + i, 2, line, i == a->dcur ? VT_A_REV : 0, s->cols - 4);
    }
    vt_draw_bottom(a, "AVAIL", "Up/Down choose   Return log this disk", "F10 back");
}

void vt_app_draw(vt_app *a)
{
    vt_screen_clear(&a->scr);
    switch (a->mode) {
    case M_VIEW: vt_view_fill(a, a->scr.rows - 4, a->scr.cols - 2); break;
    case M_INFO: draw_text(a, "File information", "INFO"); break;
    case M_HELP: draw_text(a, "Help", "HELP"); break;
    case M_AVAIL: draw_avail(a); break;
    default: draw_main(a); break;
    }
}
