/* vt_app.c - VTREE's state, reading the tree, the file lists and the keys.
   Phase 1: nothing here changes a file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "vt_app.h"
#include "vt_util.h"

/* ---------- messages and progress ---------- */

void vt_set_msg(vt_app *a, int err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->msg, sizeof a->msg, fmt, ap);
    va_end(ap);
    a->msg_err = err;
    a->msg_progress = 0;
}

void vt_status_msg(vt_app *a, const char *what, unsigned int st)
{
    char m[160];
    vtp_message(st, m, sizeof m);
    vt_set_msg(a, 1, "%s: %s", what, m);
}

int vt_check_stop(vt_app *a)
{
    int k;
    if (a->pushback != VK_NONE) return 1;
    k = vtp_key(0);
    if (k == VK_TIMEOUT || k == VK_NONE) return 0;
    a->pushback = k;
    return 1;
}

void vt_progress(vt_app *a, const char *text)
{
    vt_set_msg(a, 0, "%s", text);
    a->msg_progress = 1;              /* cleared when the work is done */
    vt_app_draw(a);
    vtp_draw(&a->scr);
}

/* ---------- the tree ---------- */

static void rebuild_vis(vt_app *a)
{
    int n = vt_tree_visible(&a->t, a->vis, a->capvis);
    if (n > a->capvis) {
        int cap = n + 256;
        int *v = (int *) realloc(a->vis, (size_t) cap * sizeof *v);
        if (!v) { vt_set_msg(a, 1, "Out of memory"); return; }
        a->vis = v; a->capvis = cap;
        n = vt_tree_visible(&a->t, a->vis, a->capvis);
    }
    a->nvis = n;
    if (a->tcur >= a->nvis) a->tcur = a->nvis ? a->nvis - 1 : 0;
}

int vt_cur_node(vt_app *a)
{
    return a->nvis ? a->vis[a->tcur] : -1;
}

static void show_dir(vt_app *a, int node)
{
    a->kind = L_DIR;
    a->list_node = node;
    a->refs_dirty = 1;
    a->fcur = a->ftop = 0;
    a->pending_load = node >= 0 && !a->t.n[node].files.loaded;
}

static void set_cursor_node(vt_app *a, int node)
{
    int i;
    rebuild_vis(a);
    for (i = 0; i < a->nvis; i++) if (a->vis[i] == node) { a->tcur = i; break; }
    show_dir(a, vt_cur_node(a));
}

typedef struct { vt_app *a; int node; int count; } scan_ctx;

static int scan_cb(void *vctx, const vtp_entry *e)
{
    scan_ctx *c = (scan_ctx *) vctx;
    if (e->is_self || !e->is_dir) return 0;
    if (vt_tree_child(&c->a->t, c->node, e->name, e->fid, e->is_alias) < 0) return 1;
    c->count++;
    return 0;
}

/* Read a node's subdirectories (once). Returns the status. */
static unsigned int scan_node(vt_app *a, int node)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    scan_ctx c;
    unsigned int st;
    if (a->t.n[node].flags & VT_N_SCANNED) return VTP_OK;
    vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
    c.a = a; c.node = node; c.count = 0;
    st = vtp_list(dev, dir, "*.DIR;1", 0, scan_cb, &c);
    if (st & 1) {
        a->t.n[node].flags |= VT_N_SCANNED;
        a->t.n[node].flags &= (unsigned char) ~VT_N_ERROR;
    } else {
        a->t.n[node].flags |= VT_N_ERROR;
        a->t.n[node].scan_status = st;
    }
    return st;
}

/* Every node of a branch, depth first, through an explicit stack. Alias nodes
   below the start are never entered (D8). Returns 0 if stopped by a key. */
static int scan_branch(vt_app *a, int start, int open)
{
    int *stack, sp = 0, cap = 1024, done = 0, ok = 1;
    stack = (int *) malloc((size_t) cap * sizeof *stack);
    if (!stack) { vt_set_msg(a, 1, "Out of memory"); return 0; }
    stack[sp++] = start;
    while (sp) {
        int n = stack[--sp], c;
        if (n != start && (a->t.n[n].flags & VT_N_ALIAS)) continue;
        if (!(a->t.n[n].flags & VT_N_SCANNED)) {
            scan_node(a, n);
            if (++done % 25 == 0) {
                char m[80];
                sprintf(m, "Reading directories: %d   (any key stops)", done);
                vt_progress(a, m);
                if (vt_check_stop(a)) { ok = 0; break; }
            }
        }
        if (open && a->t.n[n].child >= 0) a->t.n[n].flags |= VT_N_OPEN;
        for (c = a->t.n[n].child; c >= 0; c = a->t.n[c].next) {
            if (sp == cap) {
                int *ns = (int *) realloc(stack, (size_t) (cap *= 2) * sizeof *ns);
                if (!ns) { ok = 0; sp = 0; break; }
                stack = ns;
            }
            stack[sp++] = c;
        }
    }
    free(stack);
    if (!ok) vt_set_msg(a, 1, "Stopped: part of the branch is not read");
    else if (done >= 25) vt_set_msg(a, 0, "%d directories read", done);
    return ok;
}

/* ---------- file lists ---------- */

typedef struct { vt_app *a; int node; int count; int stopped; } files_ctx;

static int files_cb(void *vctx, const vtp_entry *e)
{
    files_ctx *c = (files_ctx *) vctx;
    vt_app *a = c->a;
    unsigned char fl = 0;
    if (e->is_self) return 0;
    if (e->is_dir) fl |= VT_F_DIR;
    if (e->is_alias) fl |= VT_F_ALIAS;
    if (!vt_flist_add(&a->t.n[c->node].files, e->name, e->type, e->ver, e->fid, fl, &e->a))
        return 1;
    if (e->is_dir && e->ver == 1)
        vt_tree_child(&a->t, c->node, e->name, e->fid, e->is_alias);
    if (++c->count % 500 == 0) {
        char m[80];
        sprintf(m, "Reading files: %d   (any key stops)", c->count);
        vt_progress(a, m);
        if (vt_check_stop(a)) { c->stopped = 1; return 1; }
    }
    return 0;
}

/* Read (or reread) a directory's files with their attributes. Tags survive a
   reread for files whose file ID is still there. */
static unsigned int load_files(vt_app *a, int node)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    files_ctx c;
    unsigned int st;
    unsigned short (*tagged)[3] = 0;
    int ntag = 0, i, j;
    vt_flist *fl = &a->t.n[node].files;

    if (fl->n) {
        tagged = (unsigned short (*)[3]) malloc((size_t) fl->n * sizeof *tagged);
        for (i = 0; tagged && i < fl->n; i++)
            if (fl->f[i].flags & VT_F_TAGGED) memcpy(tagged[ntag++], fl->f[i].fid, 6);
    }
    vt_flist_clear(fl);
    vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
    c.a = a; c.node = node; c.count = 0; c.stopped = 0;
    st = vtp_list(dev, dir, "*.*;*", 1, files_cb, &c);
    fl = &a->t.n[node].files;                    /* the node array may have moved */
    fl->loaded = 1;
    fl->complete = (st & 1) != 0;
    fl->status = st;
    vt_flist_mark_highest(fl);
    if (st & 1) a->t.n[node].flags |= VT_N_SCANNED;
    for (i = 0; i < ntag && ntag <= 4000; i++)
        for (j = 0; j < fl->n; j++)
            if (memcmp(fl->f[j].fid, tagged[i], 6) == 0) { fl->f[j].flags |= VT_F_TAGGED; break; }
    free(tagged);
    if (c.stopped) vt_set_msg(a, 1, "Stopped: %s%s shows only %d files", dev, dir, fl->n);
    else if (!(st & 1)) vt_status_msg(a, dir, st);
    a->refs_dirty = 1;
    return st;
}

static int ensure_files(vt_app *a, int node)
{
    if (!a->t.n[node].files.loaded) load_files(a, node);
    return a->pushback == VK_NONE;
}

vt_file *vt_ref_file(vt_app *a, const vt_ref *r)
{
    return &a->t.n[r->node].files.f[r->idx];
}

void vt_ref_path(vt_app *a, const vt_ref *r, char *out, int size)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    vt_file *f = vt_ref_file(a, r);
    vt_tree_spec(&a->t, r->node, dev, sizeof dev, dir, sizeof dir);
    snprintf(out, (size_t) size, "%s%s%s%s;%u", dev, dir, f->name, f->type, f->ver);
}

/* Nodes a list covers: the directory, the branch, or the disk (alias nodes left
   out except as a plain DIR list). */
static int list_includes(vt_app *a, int node)
{
    int root;
    if (a->kind == L_DIR) return node == a->list_node;
    if (node != a->list_node && (a->t.n[node].flags & VT_N_ALIAS)) return 0;
    if (a->kind == L_BRANCH) return vt_tree_within(&a->t, a->list_node, node);
    for (root = a->list_node; a->t.n[root].parent >= 0; root = a->t.n[root].parent) ;
    return vt_tree_within(&a->t, root, node);
}

static vt_app *sort_app;

static int cmp_name(const vt_file *x, const vt_file *y)
{
    int c = vt_strcasecmp(x->name, y->name);
    if (!c) c = vt_strcasecmp(x->type, y->type);
    if (!c) c = (int) y->ver - (int) x->ver;      /* highest version first */
    return c;
}

static int cmp_refs(const void *p, const void *q)
{
    const vt_ref *r1 = (const vt_ref *) p, *r2 = (const vt_ref *) q;
    const vt_file *x = vt_ref_file(sort_app, r1), *y = vt_ref_file(sort_app, r2);
    int c = 0;
    switch (sort_app->sort) {
    case S_TYPE: c = vt_strcasecmp(x->type, y->type); break;
    case S_SIZE: c = x->a.eof_blocks < y->a.eof_blocks ? -1 : x->a.eof_blocks > y->a.eof_blocks; break;
    case S_DATE: c = x->a.rdt < y->a.rdt ? -1 : x->a.rdt > y->a.rdt; break;
    default: break;
    }
    if (!c) c = cmp_name(x, y);
    if (!c) c = r1->node - r2->node;
    return sort_app->sort_rev ? -c : c;
}

void vt_rebuild_refs(vt_app *a)
{
    int node, i;
    a->nrefs = 0;
    a->all_files = 0; a->all_blocks = 0; a->match_blocks = 0;
    if (a->list_node < 0) { a->refs_dirty = 0; return; }
    for (node = 0; node < a->t.count; node++) {
        vt_flist *fl = &a->t.n[node].files;
        if (!fl->loaded || !list_includes(a, node)) continue;
        for (i = 0; i < fl->n; i++) {
            vt_file *f = &fl->f[i];
            if (f->flags & VT_F_DIR) continue;          /* directories are in the tree */
            a->all_files++;
            a->all_blocks += f->a.eof_blocks;
            if (a->highest_only && !(f->flags & VT_F_HIGHEST)) continue;
            if (!vt_filter_match(&a->filter, f->name, f->type, f->ver,
                                 (f->flags & VT_F_HIGHEST) != 0))
                continue;
            if (a->nrefs == a->caprefs) {
                int cap = a->caprefs ? a->caprefs * 2 : 256;
                vt_ref *nr = (vt_ref *) realloc(a->refs, (size_t) cap * sizeof *nr);
                if (!nr) { vt_set_msg(a, 1, "Out of memory"); goto done; }
                a->refs = nr; a->caprefs = cap;
            }
            a->refs[a->nrefs].node = node;
            a->refs[a->nrefs].idx = i;
            a->nrefs++;
            a->match_blocks += f->a.eof_blocks;
        }
    }
done:
    sort_app = a;
    if (a->nrefs > 1) qsort(a->refs, (size_t) a->nrefs, sizeof *a->refs, cmp_refs);
    if (a->fcur >= a->nrefs) a->fcur = a->nrefs ? a->nrefs - 1 : 0;
    a->refs_dirty = 0;
}

/* Read every directory of the list (branch or showall). 0 if stopped. */
static int load_list(vt_app *a)
{
    int node, n = 0;
    for (node = 0; node < a->t.count; node++) {
        if (!list_includes(a, node) || a->t.n[node].files.loaded) continue;
        load_files(a, node);
        if (++n % 10 == 0) {
            char m[80];
            sprintf(m, "Reading files of directory %d   (any key stops)", n);
            vt_progress(a, m);
        }
        if (vt_check_stop(a)) { vt_set_msg(a, 1, "Stopped: the list is not complete"); return 0; }
    }
    a->refs_dirty = 1;
    return 1;
}

/* ---------- logging a disk or directory ---------- */

static void log_spec(vt_app *a, const char *spec)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    static char comp[64][VT_NAME_MAX];
    int root, node, n, i;
    unsigned int st = vtp_locate(spec, dev, sizeof dev, dir, sizeof dir);
    if (!(st & 1)) { vt_status_msg(a, spec && *spec ? spec : "default directory", st); return; }
    root = vt_tree_root(&a->t, dev);
    if (root < 0) { vt_set_msg(a, 1, "Out of memory"); return; }
    node = root;
    n = vt_dir_split(dir, comp, 64);
    for (i = 0; i < n; i++) {
        int c;
        scan_node(a, node);
        c = vt_tree_find_child(&a->t, node, comp[i]);
        if (c < 0) c = vt_tree_child(&a->t, node, comp[i], 0, 0);   /* listing denied */
        if (c < 0) break;
        a->t.n[node].flags |= VT_N_OPEN;
        node = c;
    }
    scan_node(a, node);
    if (a->t.n[node].child >= 0) a->t.n[node].flags |= VT_N_OPEN;
    set_cursor_node(a, node);
    if (a->t.n[node].flags & VT_N_ERROR) vt_status_msg(a, dir, a->t.n[node].scan_status);
}

/* ---------- setup ---------- */

int vt_app_init(vt_app *a, const char *spec, int rows, int cols)
{
    memset(a, 0, sizeof *a);
    vt_tree_init(&a->t);
    a->pushback = VK_NONE;
    a->list_node = -1;
    vt_filter_parse(&a->filter, "*.*;*");
    if (!vt_screen_init(&a->scr, rows, cols)) return 0;
    a->rows = rows; a->cols = cols;
    log_spec(a, spec);
    return a->t.count > 0;
}

void vt_app_free(vt_app *a)
{
    vt_view_close(a);
    vt_text_free(a);
    vt_tree_free(&a->t);
    free(a->vis); free(a->refs);
    vt_screen_free(&a->scr);
}

void vt_app_resize(vt_app *a, int rows, int cols)
{
    vt_screen_free(&a->scr);
    if (vt_screen_init(&a->scr, rows, cols)) { a->rows = rows; a->cols = cols; }
}

/* ---------- text pages (Info, Help) ---------- */

void vt_text_free(vt_app *a)
{
    int i;
    for (i = 0; i < a->ninfo; i++) free(a->info[i]);
    free(a->info);
    a->info = 0; a->ninfo = 0; a->itop = 0;
}

void vt_text_add(vt_app *a, const char *line)
{
    char **ni = (char **) realloc(a->info, (size_t) (a->ninfo + 1) * sizeof *ni);
    size_t n = strlen(line) + 1;
    if (!ni) return;
    a->info = ni;
    if ((a->info[a->ninfo] = (char *) malloc(n)) != 0) {
        memcpy(a->info[a->ninfo], line, n);
        a->ninfo++;
    }
}

/* ---------- prompts ---------- */

static void start_prompt(vt_app *a, int kind, const char *label, const char *initial)
{
    a->prompt = kind;
    vt_strlcpy(a->prompt_label, label, sizeof a->prompt_label);
    vt_edit_init(&a->edit, initial);
}

static void prompt_key(vt_app *a, int key)
{
    int kind = a->prompt, r;
    if (kind == P_SORT || kind == P_QUIT) {          /* one-key choices */
        int k = (key >= 'a' && key <= 'z') ? key - 32 : key;
        a->prompt = P_NONE;
        if (kind == P_QUIT) { if (k == 'Y') a->quit = 1; return; }
        switch (k) {
        case 'N': a->sort = S_NAME; break;
        case 'T': a->sort = S_TYPE; break;
        case 'S': a->sort = S_SIZE; break;
        case 'D': a->sort = S_DATE; break;
        case 'R': a->sort_rev = !a->sort_rev; break;
        default: return;
        }
        a->refs_dirty = 1;
        return;
    }
    r = vt_edit_key(&a->edit, key);
    if (r == VT_EDIT_MORE) return;
    a->prompt = P_NONE;
    if (r == VT_EDIT_CANCEL) return;
    switch (kind) {
    case P_FILESPEC:
        if (!vt_filter_parse(&a->filter, a->edit.buf))
            vt_set_msg(a, 1, "Not a valid filespec: %s (name.type;version, comma list)",
                       a->edit.buf);
        a->refs_dirty = 1;
        a->fcur = a->ftop = 0;
        break;
    case P_LOG:
        a->mode = M_TREE;
        log_spec(a, a->edit.buf);
        break;
    case P_FIND:
        vt_strlcpy(a->view.find, a->edit.buf, sizeof a->view.find);
        vt_view_key(a, VK_PF3);
        break;
    default: break;
    }
}

/* ---------- tagging ---------- */

static long tag_refs(vt_app *a, int on)
{
    long i, n = 0;
    for (i = 0; i < a->nrefs; i++) {
        vt_file *f = vt_ref_file(a, &a->refs[i]);
        if (on) f->flags |= VT_F_TAGGED; else f->flags &= (unsigned char) ~VT_F_TAGGED;
        n++;
    }
    return n;
}

/* ---------- keys: tree window ---------- */

static int tree_rows(vt_app *a)
{
    int inner = a->rows - 5;               /* title, box top/bottom, two menu lines */
    int frows = inner / 3 < 3 ? 3 : inner / 3;
    return inner - frows - 1;
}

static void open_avail(vt_app *a);

static void tree_key(vt_app *a, int key, int gold)
{
    int node = vt_cur_node(a), page = tree_rows(a), moved = 0, old = a->tcur;
    if (node < 0) return;
    switch (key) {
    case VK_UP: if (gold) a->tcur = 0; else if (a->tcur > 0) a->tcur--; break;
    case VK_DOWN: if (gold) a->tcur = a->nvis - 1; else if (a->tcur < a->nvis - 1) a->tcur++; break;
    case VK_PREV: a->tcur = a->tcur > page ? a->tcur - page : 0; break;
    case VK_NEXT: a->tcur = a->tcur + page < a->nvis ? a->tcur + page : a->nvis - 1; break;
    case VK_LEFT:
        if (a->t.n[node].parent >= 0) { set_cursor_node(a, a->t.n[node].parent); return; }
        break;
    case VK_RIGHT: case '+':
        scan_node(a, node);
        if (a->t.n[node].flags & VT_N_ERROR) { vt_status_msg(a, a->t.n[node].name, a->t.n[node].scan_status); break; }
        if (a->t.n[node].child < 0) { if (key == '+') vt_set_msg(a, 0, "No subdirectories"); break; }
        a->t.n[node].flags |= VT_N_OPEN;
        rebuild_vis(a);
        if (key == VK_RIGHT) set_cursor_node(a, a->t.n[node].child);
        return;
    case '-':
        a->t.n[node].flags &= (unsigned char) ~VT_N_OPEN;
        rebuild_vis(a);
        return;
    case '*':
        scan_branch(a, node, 1);
        rebuild_vis(a);
        return;
    case VK_RETURN: case VK_ENTER:
        ensure_files(a, node);
        a->mode = M_FILES;
        return;
    case 'B': case 'b':
        if (!scan_branch(a, node, 0)) return;
        a->kind = L_BRANCH; a->list_node = node; a->fcur = a->ftop = 0;
        load_list(a);
        a->refs_dirty = 1; a->mode = M_FILES;
        return;
    case 'S': case 's':
        if (gold) { start_prompt(a, P_SORT, "Sort by: N name  T type  S size  D date  R reverse", ""); return; }
        a->kind = L_SHOWALL; a->list_node = node; a->fcur = a->ftop = 0;
        load_list(a);
        a->refs_dirty = 1; a->mode = M_FILES;
        return;
    case 'L': case 'l':
        if (gold) {                          /* the whole disk, as XTree did at start-up */
            int r = node;
            while (a->t.n[r].parent >= 0) r = a->t.n[r].parent;
            scan_branch(a, r, 1);
            rebuild_vis(a);
            return;
        }
        { char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], both[2 * VT_SPEC_MAX];
          vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
          snprintf(both, sizeof both, "%s%s", dev, dir);
          start_prompt(a, P_LOG, "Log disk or directory: ", both); }
        return;
    case 'A': case 'a': open_avail(a); return;
    case 'T': case 't': case 'U': case 'u': case VK_SELECT: {
        int on = !(key == 'U' || key == 'u');
        long n;
        if (gold) {
            if (!scan_branch(a, node, 0)) return;
            a->kind = L_BRANCH; a->list_node = node;
            if (!load_list(a)) { show_dir(a, node); return; }
        } else if (!ensure_files(a, node)) return;
        vt_rebuild_refs(a);
        n = tag_refs(a, on);
        vt_set_msg(a, 0, "%s %ld file%s%s", on ? "Tagged" : "Untagged", n, n == 1 ? "" : "s",
                   gold ? " in the branch" : "");
        if (gold) show_dir(a, node);
        a->refs_dirty = 1;
        return;
    }
    case 'R': case 'r':
        if (gold) { a->t.n[node].flags &= (unsigned char) ~VT_N_SCANNED; load_files(a, node); rebuild_vis(a); }
        return;
    default: return;
    }
    moved = a->tcur != old;
    if (moved) show_dir(a, vt_cur_node(a));
}

/* ---------- keys: file window ---------- */

static void file_key(vt_app *a, int key, int gold)
{
    int cols = a->fcols > 0 ? a->fcols : 1, page = (a->frows > 0 ? a->frows : 10) * cols;
    vt_ref *r = a->nrefs ? &a->refs[a->fcur] : 0;
    switch (key) {
    case VK_UP: if (gold) a->fcur = 0; else if (a->fcur >= cols) a->fcur -= cols; break;
    case VK_DOWN:
        if (gold) a->fcur = a->nrefs ? a->nrefs - 1 : 0;
        else if (a->fcur + cols < a->nrefs) a->fcur += cols;
        break;
    case VK_LEFT: if (a->fcur > 0) a->fcur--; break;
    case VK_RIGHT: if (a->fcur + 1 < a->nrefs) a->fcur++; break;
    case VK_PREV: a->fcur = a->fcur > page ? a->fcur - page : 0; break;
    case VK_NEXT: a->fcur = a->fcur + page < a->nrefs ? a->fcur + page : (a->nrefs ? a->nrefs - 1 : 0); break;
    case VK_RETURN: case VK_ENTER: case VK_F10: case VK_CTRL_Z:
        a->mode = M_TREE;
        show_dir(a, vt_cur_node(a));
        break;
    case 'T': case 't': case 'U': case 'u': case VK_SELECT: {
        int on = !(key == 'U' || key == 'u');
        if (gold) {
            long n = tag_refs(a, on);
            vt_set_msg(a, 0, "%s %ld file%s", on ? "Tagged" : "Untagged", n, n == 1 ? "" : "s");
            break;
        }
        if (!r) break;
        if (key == VK_SELECT) on = !(vt_ref_file(a, r)->flags & VT_F_TAGGED);
        if (on) vt_ref_file(a, r)->flags |= VT_F_TAGGED;
        else vt_ref_file(a, r)->flags &= (unsigned char) ~VT_F_TAGGED;
        if (a->fcur + 1 < a->nrefs) a->fcur++;
        break;
    }
    case 'V': case 'v': if (r) vt_view_open(a, r); break;
    case 'I': case 'i': if (r) { vt_info_build(a, r); a->back_mode = M_FILES; a->mode = M_INFO; } break;
    case '=': a->fmt = !a->fmt; a->ftop = 0; break;
    case 'S': case 's':
        if (gold) start_prompt(a, P_SORT, "Sort by: N name  T type  S size  D date  R reverse", "");
        break;
    case 'R': case 'r':
        if (gold && r) { int n = r->node; load_files(a, n); }
        break;
    default: break;
    }
}

/* ---------- Info on a directory (from the tree) ---------- */

static void dir_info(vt_app *a, int node)
{
    int parent = a->t.n[node].parent, i;
    vt_flist *fl;
    if (parent < 0) {
        vtp_device d;
        char m[200], b1[32], b2[32];
        unsigned int st = vtp_device_info(a->t.n[node].name, &d);
        if (!(st & 1)) { vt_status_msg(a, a->t.n[node].name, st); return; }
        vt_text_free(a);
        snprintf(m, sizeof m, "Disk %s   label %s   %s", d.name, d.label, d.logvol); vt_text_add(a, m);
        snprintf(m, sizeof m, "Free blocks %s of %s", vt_commas(d.free_blocks, b1, sizeof b1),
                 vt_commas(d.max_blocks, b2, sizeof b2)); vt_text_add(a, m);
        if (d.ods) { snprintf(m, sizeof m, "Structure ODS-%d", d.ods); vt_text_add(a, m); }
        a->back_mode = M_TREE; a->mode = M_INFO;
        return;
    }
    /* The directory file is an entry of its parent's file list. */
    if (!a->t.n[parent].files.loaded) load_files(a, parent);
    fl = &a->t.n[parent].files;
    for (i = 0; i < fl->n; i++)
        if ((fl->f[i].flags & VT_F_DIR) && fl->f[i].ver == 1
            && vt_strcasecmp(fl->f[i].name, a->t.n[node].name) == 0) {
            vt_ref r;
            r.node = parent; r.idx = i;
            vt_info_build(a, &r);
            a->back_mode = M_TREE; a->mode = M_INFO;
            return;
        }
    vt_set_msg(a, 1, "%s.DIR;1 is not in its parent's listing", a->t.n[node].name);
}

/* ---------- Avail: the mounted disks ---------- */

static int dev_cb(void *vctx, const vtp_device *d)
{
    vt_app *a = (vt_app *) vctx;
    if (a->ndevs < (int) (sizeof a->devs / sizeof a->devs[0])) a->devs[a->ndevs++] = *d;
    return 0;
}

static void open_avail(vt_app *a)
{
    unsigned int st;
    a->ndevs = 0; a->dcur = 0;
    st = vtp_devices(dev_cb, a);
    if (!(st & 1) && !a->ndevs) { vt_status_msg(a, "Disks", st); return; }
    a->back_mode = a->mode;
    a->mode = M_AVAIL;
}

static void avail_key(vt_app *a, int key)
{
    switch (key) {
    case VK_UP: if (a->dcur > 0) a->dcur--; break;
    case VK_DOWN: if (a->dcur + 1 < a->ndevs) a->dcur++; break;
    case VK_RETURN: case VK_ENTER: case 'L': case 'l':
        if (a->ndevs) {
            char spec[80];
            snprintf(spec, sizeof spec, "%s[000000]", a->devs[a->dcur].name);
            a->mode = M_TREE;
            log_spec(a, spec);
        }
        break;
    case VK_F10: case VK_CTRL_Z: case 'Q': case 'q': a->mode = a->back_mode; break;
    default: break;
    }
}

/* ---------- Info / Help pages ---------- */

static void text_key(vt_app *a, int key)
{
    int page = a->rows - 4;
    switch (key) {
    case VK_UP: if (a->itop > 0) a->itop--; break;
    case VK_DOWN: if (a->itop + page < a->ninfo) a->itop++; break;
    case VK_PREV: a->itop = a->itop > page ? a->itop - page : 0; break;
    case VK_NEXT: if (a->itop + page < a->ninfo) a->itop += page; break;
    default: a->mode = a->back_mode; vt_text_free(a); break;
    }
}

/* ---------- dispatch ---------- */

void vt_app_key(vt_app *a, int key)
{
    int gold = a->gold;
    a->msg[0] = '\0';
    if (a->prompt != P_NONE) { prompt_key(a, key); return; }
    if (key == VK_CTRL_W) { vtp_repaint(); return; }
    if (a->mode == M_VIEW) { vt_view_key(a, key); return; }   /* it has its own GOLD */
    if (key == VK_PF1) { a->gold = !a->gold; return; }
    a->gold = 0;

    switch (a->mode) {
    case M_INFO: case M_HELP: text_key(a, key); return;
    case M_AVAIL: avail_key(a, key); return;
    default: break;
    }

    switch (key) {                               /* keys of both windows */
    case VK_HELP: case VK_PF2: case '?':
        vt_help_build(a, a->mode);
        a->back_mode = a->mode; a->mode = M_HELP;
        return;
    case VK_FIND: case 'F': case 'f':
        start_prompt(a, P_FILESPEC, "Filespec: ", a->filter.text);
        return;
    case ';':
        a->highest_only = !a->highest_only;
        a->refs_dirty = 1;
        vt_set_msg(a, 0, a->highest_only ? "Highest versions only" : "All versions");
        return;
    case 'Q': case 'q':
        start_prompt(a, P_QUIT, "Quit VTREE?  Y yes  N no", "");
        return;
    case VK_F10: case VK_CTRL_Z:
        if (a->mode == M_TREE) { a->quit = 1; return; }
        break;
    case 'I': case 'i':
        if (a->mode == M_TREE) { dir_info(a, vt_cur_node(a)); return; }
        break;
    default: break;
    }
    if (a->mode == M_TREE) tree_key(a, key, gold);
    else file_key(a, key, gold);
    if (a->msg_progress) { a->msg[0] = '\0'; a->msg_progress = 0; }
}

int vt_app_idle(vt_app *a)
{
    if (a->pending_load && a->list_node >= 0 && (a->mode == M_TREE || a->mode == M_FILES)) {
        a->pending_load = 0;
        if (!a->t.n[a->list_node].files.loaded) {
            load_files(a, a->list_node);
            if (a->msg_progress) { a->msg[0] = '\0'; a->msg_progress = 0; }
            return 1;
        }
    }
    return 0;
}

static int next_key(vt_app *a, int timeout)
{
    int k;
    if (a->pushback != VK_NONE) { k = a->pushback; a->pushback = VK_NONE; return k; }
    return vtp_key(timeout);
}

void vt_app_run(vt_app *a)
{
    char bc[256];
    while (!a->quit) {
        int k, r, c;
        if (vtp_broadcast(bc, sizeof bc)) vt_set_msg(a, 0, "%s", bc);
        vtp_screen_size(&r, &c);
        if (r != a->rows || c != a->cols) vt_app_resize(a, r, c);
        vt_app_draw(a);
        vtp_draw(&a->scr);
        k = next_key(a, 0);
        if (k == VK_TIMEOUT) {
            if (vt_app_idle(a)) continue;
            k = next_key(a, 10);              /* the clock moves on a timeout */
            if (k == VK_TIMEOUT) continue;
        }
        vt_app_key(a, k);
    }
}
