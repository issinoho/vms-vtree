/* vt_tree.c - directory tree and file lists. No I/O. */
#include <stdlib.h>
#include <string.h>
#include "vt_tree.h"
#include "vt_util.h"

static char *dupstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *) malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

void vt_tree_init(vt_tree *t)
{
    memset(t, 0, sizeof *t);
    t->first_root = -1;
}

void vt_tree_free(vt_tree *t)
{
    int i;
    for (i = 0; i < t->count; i++) {
        vt_flist_clear(&t->n[i].files);
        free(t->n[i].files.f);
        free(t->n[i].name);
    }
    free(t->n);
    vt_tree_init(t);
}

static int new_node(vt_tree *t, const char *name)
{
    vt_node *n;
    if (t->count == t->cap) {
        int cap = t->cap ? t->cap * 2 : 256;
        vt_node *nn = (vt_node *) realloc(t->n, (size_t) cap * sizeof *nn);
        if (!nn) return -1;
        t->n = nn; t->cap = cap;
    }
    n = &t->n[t->count];
    memset(n, 0, sizeof *n);
    n->name = dupstr(name);
    if (!n->name) return -1;
    n->parent = n->child = n->next = -1;
    return t->count++;
}

int vt_tree_root(vt_tree *t, const char *device)
{
    int i, last = -1, r;
    for (i = t->first_root; i >= 0; i = t->n[i].next) {
        if (vt_strcasecmp(t->n[i].name, device) == 0) return i;
        last = i;
    }
    r = new_node(t, device);
    if (r < 0) return -1;
    t->n[r].flags = VT_N_ROOT;
    if (last < 0) t->first_root = r; else t->n[last].next = r;
    return r;
}

int vt_tree_find_child(const vt_tree *t, int parent, const char *name)
{
    int i;
    for (i = t->n[parent].child; i >= 0; i = t->n[i].next)
        if (vt_strcasecmp(t->n[i].name, name) == 0) return i;
    return -1;
}

int vt_tree_child(vt_tree *t, int parent, const char *name, const unsigned short fid[3],
                  int alias)
{
    int c = vt_tree_find_child(t, parent, name), prev = -1, i;
    if (c >= 0) return c;
    c = new_node(t, name);            /* may move t->n */
    if (c < 0) return -1;
    t->n[c].parent = parent;
    t->n[c].depth = t->n[parent].depth + 1;
    if (fid) memcpy(t->n[c].fid, fid, sizeof t->n[c].fid);
    if (alias == 1) t->n[c].flags |= VT_N_ALIAS;
    else if (alias == 2) t->n[c].flags |= VT_N_NOLINK;
    for (i = t->n[parent].child; i >= 0 && vt_strcasecmp(t->n[i].name, name) < 0;
         i = t->n[i].next)
        prev = i;
    t->n[c].next = i;
    if (prev < 0) t->n[parent].child = c; else t->n[prev].next = c;
    return c;
}

void vt_tree_spec(const vt_tree *t, int node, char *dev, size_t devsz, char *dir, size_t dirsz)
{
    int chain[256], depth = 0, i;
    char tmp[VT_SPEC_MAX];
    while (node >= 0 && !(t->n[node].flags & VT_N_ROOT) && depth < 256) {
        chain[depth++] = node;
        node = t->n[node].parent;
    }
    vt_strlcpy(dev, node >= 0 ? t->n[node].name : "", devsz);
    vt_strlcpy(dir, "[000000]", dirsz);
    for (i = depth - 1; i >= 0; i--) {
        if (!vt_dir_child(dir, t->n[chain[i]].name, tmp, sizeof tmp)) break;
        vt_strlcpy(dir, tmp, dirsz);
    }
}

static int visit(const vt_tree *t, int i, int *out, int max, int n)
{
    for (; i >= 0; i = t->n[i].next) {
        if (n < max) out[n] = i;
        n++;
        if ((t->n[i].flags & VT_N_OPEN) && t->n[i].child >= 0)
            n = visit(t, t->n[i].child, out, max, n);
    }
    return n;
}

int vt_tree_visible(const vt_tree *t, int *out, int max)
{
    return visit(t, t->first_root, out, max, 0);
}

int vt_tree_is_last(const vt_tree *t, int node) { return t->n[node].next < 0; }

int vt_tree_within(const vt_tree *t, int a, int b)
{
    for (; b >= 0; b = t->n[b].parent) if (b == a) return 1;
    return 0;
}

int vt_tree_reaches(const vt_tree *t, int a, int b)
{
    for (; b >= 0; b = t->n[b].parent) {
        if (b == a) return 1;
        if (t->n[b].flags & VT_N_CLOSED) return 0;
    }
    return 0;
}

int vt_tree_reach_top(const vt_tree *t, int node)
{
    while (node >= 0 && !(t->n[node].flags & VT_N_CLOSED) && t->n[node].parent >= 0)
        node = t->n[node].parent;
    return node;
}

int vt_flist_add(vt_flist *fl, const char *name, const char *type, unsigned int ver,
                 const unsigned short fid[3], unsigned char flags, const vt_attrs *a)
{
    vt_file *f;
    size_t nl = strlen(name) + 1, tl = strlen(type) + 1;
    if (fl->n == fl->cap) {
        int cap = fl->cap ? fl->cap * 2 : 64;
        vt_file *nf = (vt_file *) realloc(fl->f, (size_t) cap * sizeof *nf);
        if (!nf) return 0;
        fl->f = nf; fl->cap = cap;
    }
    f = &fl->f[fl->n];
    memset(f, 0, sizeof *f);
    f->name = (char *) malloc(nl + tl);           /* name and type in one block */
    if (!f->name) return 0;
    memcpy(f->name, name, nl);
    f->type = f->name + nl;
    memcpy(f->type, type, tl);
    f->ver = (unsigned short) ver;
    if (fid) memcpy(f->fid, fid, sizeof f->fid);
    f->flags = flags;
    if (a) f->a = *a;
    fl->n++;
    return 1;
}

void vt_flist_clear(vt_flist *fl)
{
    int i;
    for (i = 0; i < fl->n; i++) free(fl->f[i].name);
    fl->n = 0;
    fl->loaded = fl->complete = 0;
}

void vt_flist_mark_highest(vt_flist *fl)
{
    int i;
    for (i = 0; i < fl->n; i++) {
        vt_file *f = &fl->f[i], *p = i ? &fl->f[i - 1] : 0;
        if (!p || vt_strcasecmp(p->name, f->name) || vt_strcasecmp(p->type, f->type))
            f->flags |= VT_F_HIGHEST;
        else
            f->flags &= (unsigned char) ~VT_F_HIGHEST;
    }
}
