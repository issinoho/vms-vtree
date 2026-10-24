/* vt_ops.c - the operations that change files, Phase 2: make directory, copy,
   move within a disk, rename, edit, and a DCL subprocess. Nothing here deletes or
   overwrites (D12): every check is made before the first change, each change is
   logged, and a failure asks whether to skip or stop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vt_app.h"
#include "vt_util.h"
#include "vt_plan.h"

/* ---------- small helpers ---------- */

int vt_find_node(vt_app *a, const char *dev, const char *dir)
{
    static char comp[64][VT_NAME_MAX];
    int node, n, i;
    for (node = a->t.first_root; node >= 0; node = a->t.n[node].next)
        if (vt_strcasecmp(a->t.n[node].name, dev) == 0) break;
    if (node < 0) return -1;
    n = vt_dir_split(dir, comp, 64);
    for (i = 0; i < n && node >= 0; i++) node = vt_tree_find_child(&a->t, node, comp[i]);
    return node;
}

void vt_log_op(vt_app *a, const char *what, const char *from, const char *to, unsigned int st)
{
    char t[32], m[160], line[3 * VT_SPEC_MAX];
    (void) a;
    vt_fmt_time(vtp_now(), 1, t, sizeof t);
    vtp_message(st, m, sizeof m);
    if (to) snprintf(line, sizeof line, "%s %-6s %s -> %s  %s", t, what, from, to, m);
    else snprintf(line, sizeof line, "%s %-6s %s  %s", t, what, from, m);
    vtp_log(line);
}

/* Read a node's files again if they were read before (the operation changed them). */
void vt_refresh(vt_app *a, int node)
{
    if (node < 0) return;
    if (a->t.n[node].files.loaded) vt_load_files(a, node);
    a->refs_dirty = 1;
}

static void untag(vt_app *a, const vt_src *s)
{
    vt_flist *fl;
    int i;
    if (s->node < 0 || s->node >= a->t.count) return;
    fl = &a->t.n[s->node].files;
    for (i = 0; i < fl->n; i++)
        if (memcmp(fl->f[i].fid, s->fid, 6) == 0) fl->f[i].flags &= (unsigned char) ~VT_F_TAGGED;
}

static void free_op(vt_app *a)
{
    free(a->op_src);
    a->op_src = 0; a->op_n = 0; a->op_kind = 0;
}

/* ---------- questions ---------- */

/* Ask and wait for one of choices (upper case). Return, F10 and Ctrl/Z give def,
   so the safe answer is always the default. */
int vt_ask(vt_app *a, const char *question, const char *choices, int def)
{
    int idle = 0, k;
    int saved = a->prompt;
    char label[sizeof a->prompt_label];
    vt_strlcpy(label, a->prompt_label, sizeof label);
    a->prompt = P_ASK;
    vt_strlcpy(a->prompt_label, question, sizeof a->prompt_label);
    vtp_flush_keys();                       /* typeahead never answers a question */
    for (;;) {
        vt_app_draw(a);
        vtp_draw(&a->scr);
        k = vtp_key(30);
        if (k == VK_TIMEOUT) { if (++idle > 1000) { k = def; break; } continue; }
        if (k >= 'a' && k <= 'z') k -= 32;
        if (k == VK_RETURN || k == VK_ENTER || k == VK_F10 || k == VK_CTRL_Z) { k = def; break; }
        if (k > 0 && k < 256 && strchr(choices, k)) break;
    }
    a->prompt = saved;
    vt_strlcpy(a->prompt_label, label, sizeof a->prompt_label);
    return k;
}

/* ---------- choosing the sources ---------- */

int vt_collect(vt_app *a, int tagged)
{
    int i, n = 0;
    free_op(a);
    if (a->refs_dirty) vt_rebuild_refs(a);
    if (!a->nrefs) return 0;
    a->op_src = (vt_src *) calloc((size_t) (tagged ? a->nrefs : 1), sizeof (vt_src));
    if (!a->op_src) { vt_set_msg(a, 1, "Out of memory"); return 0; }
    for (i = 0; i < a->nrefs; i++) {
        vt_ref *r = &a->refs[i];
        vt_file *f = vt_ref_file(a, r);
        vt_src *s;
        if (tagged ? !(f->flags & VT_F_TAGGED) : i != a->fcur) continue;
        s = &a->op_src[n++];
        s->node = r->node;
        memcpy(s->fid, f->fid, sizeof s->fid);
        vt_ref_path(a, r, s->path, sizeof s->path);
        vt_strlcpy(s->name, f->name, sizeof s->name);
        vt_strlcpy(s->type, f->type, sizeof s->type);
        s->ver = f->ver;
        s->blocks = f->a.eof_blocks;
    }
    a->op_n = n;
    return n;
}

static void current_dir(vt_app *a, char *out, int size)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    int node = a->op_n ? a->op_src[0].node : vt_cur_node(a);
    vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
    snprintf(out, (size_t) size, "%s%s", dev, dir);
}

void vt_op_start(vt_app *a, int kind, int tagged)
{
    char label[48], def[VT_SPEC_MAX];
    int n = vt_collect(a, tagged);
    if (!n) { vt_set_msg(a, 1, tagged ? "No files are tagged in this list" : "No file here"); return; }
    a->op_kind = kind;
    if (n == 1) snprintf(def, sizeof def, "%s%s", a->op_src[0].name, a->op_src[0].type);
    else vt_strlcpy(def, "*.*", sizeof def);
    if (kind == OP_COPY) {
        snprintf(label, sizeof label, n == 1 ? "Copy as: " : "Copy %d files as: ", n);
        vt_start_prompt(a, P_COPY_AS, label, def);
    } else if (kind == OP_MOVE) {
        vt_strlcpy(a->op_pattern, "*.*", sizeof a->op_pattern);
        snprintf(label, sizeof label, n == 1 ? "Move to: " : "Move %d files to: ", n);
        current_dir(a, def, sizeof def);
        vt_start_prompt(a, P_MOVE_TO, label, def);
    } else {
        snprintf(label, sizeof label, n == 1 ? "Rename to: " : "Rename %d files to: ", n);
        if (n > 1) vt_strlcpy(def, "*.*", sizeof def);
        vt_start_prompt(a, P_RENAME, label, def);
    }
}

/* ---------- the plan: names, devices and version room, before any change ---------- */

static int cmp_src(const void *p, const void *q)
{
    const vt_src *x = (const vt_src *) p, *y = (const vt_src *) q;
    int c = vt_strcasecmp(x->oname, y->oname);
    if (!c) c = vt_strcasecmp(x->otype, y->otype);
    if (!c) c = (int) x->ver - (int) y->ver;          /* oldest first (D12) */
    return c;
}

/* Fill oname/otype; check versions in dev:dir. Returns 1 if the operation may go. */
static int plan(vt_app *a, const char *pattern, const char *dev, const char *dir, int new_versions)
{
    int i, j;
    if (a->op_n > 1 && !vt_pattern_wild(pattern)) {
        vt_set_msg(a, 1, "%d files can't all get the name %s: use * (e.g. *.BAK)", a->op_n, pattern);
        return 0;
    }
    for (i = 0; i < a->op_n; i++)
        if (!vt_name_apply(pattern, a->op_src[i].name, a->op_src[i].type, a->op_src[i].oname,
                           sizeof a->op_src[i].oname, a->op_src[i].otype, sizeof a->op_src[i].otype)) {
            vt_set_msg(a, 1, "Not a name pattern: %s (NAME.TYPE, each part a name or *)", pattern);
            return 0;
        }
    qsort(a->op_src, (size_t) a->op_n, sizeof *a->op_src, cmp_src);
    for (i = 0; i < a->op_n; i = j) {
        int count, highest, limit, incoming, room;
        for (j = i; j < a->op_n && !vt_strcasecmp(a->op_src[j].oname, a->op_src[i].oname)
                    && !vt_strcasecmp(a->op_src[j].otype, a->op_src[i].otype); j++) ;
        incoming = j - i;
        if (!(vtp_versions(dev, dir, a->op_src[i].oname, a->op_src[i].otype, &count, &highest, &limit) & 1))
            continue;
        if (!new_versions) highest = 0;              /* moves keep their numbers */
        room = vt_version_room(count, highest, incoming, limit);
        if (room == VT_ROOM_LIMIT) {
            vt_set_msg(a, 1, "Refused: %s%s has %d of its %d versions; RMS would delete the oldest",
                       a->op_src[i].oname, a->op_src[i].otype, count, limit);
            return 0;
        }
        if (room == VT_ROOM_32767) {
            vt_set_msg(a, 1, "Refused: %s%s is at version %d; no room for more",
                       a->op_src[i].oname, a->op_src[i].otype, highest);
            return 0;
        }
    }
    return 1;
}

/* The destination: an existing directory, or one the user agrees to create. */
static int destination(vt_app *a, const char *spec, char *dev, int devsz, char *dir, int dirsz)
{
    unsigned int st = vtp_locate(spec, dev, devsz, dir, dirsz);
    if (st & 1) return 1;
    {
        /* Not there: offer to create it, if the text names a directory. */
        const char *colon = strchr(spec, ':'), *lb = strchr(spec, '[');
        char q[3 * VT_SPEC_MAX], d1[VT_SPEC_MAX], d2[VT_SPEC_MAX];
        if (!lb || spec[strlen(spec) - 1] != ']') { vt_status_msg(a, spec, st); return 0; }
        if (colon && colon < lb) snprintf(d1, sizeof d1, "%.*s", (int) (colon - spec + 1), spec);
        else if (!(vtp_locate("", d1, sizeof d1, d2, sizeof d2) & 1)) { vt_status_msg(a, spec, st); return 0; }
        vt_strlcpy(d2, lb, sizeof d2);
        snprintf(q, sizeof q, "%s%s does not exist. Create it?  Y yes  N no", d1, d2);
        if (vt_ask(a, q, "YN", 'N') != 'Y') { vt_set_msg(a, 0, "Nothing done"); return 0; }
        st = vtp_mkdir(d1, d2);
        if (!(st & 1)) { vt_log_op(a, "MKDIR", spec, 0, st); vt_status_msg(a, d2, st); return 0; }
        st = vtp_locate(spec, dev, devsz, dir, dirsz);
        {
            char full[2 * VT_SPEC_MAX];
            snprintf(full, sizeof full, "%s%s", (st & 1) ? dev : d1, (st & 1) ? dir : d2);
            vt_log_op(a, "MKDIR", full, 0, VTP_CREATED);
        }
        if (!(st & 1)) { vt_status_msg(a, spec, st); return 0; }
        {   /* show it in the tree if its parent is there */
            static char comp[64][VT_NAME_MAX];
            char pd[VT_SPEC_MAX];
            int n = vt_dir_split(dir, comp, 64), i, pnode;
            vt_strlcpy(pd, "[000000]", sizeof pd);
            for (i = 0; i < n - 1; i++) { char t[VT_SPEC_MAX]; vt_dir_child(pd, comp[i], t, sizeof t); vt_strlcpy(pd, t, sizeof pd); }
            pnode = vt_find_node(a, dev, pd);
            if (pnode >= 0) {
                a->t.n[pnode].flags &= (unsigned char) ~VT_N_SCANNED;
                vt_scan_node(a, pnode);
                vt_rebuild_vis(a);
            }
        }
        return 1;
    }
}

static int same_dev(const char *a, const char *b) { return vt_strcasecmp(a, b) == 0; }

/* ---------- running it ---------- */

static void run(vt_app *a, const char *dest_spec)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], q[3 * VT_SPEC_MAX];
    const char *verb = a->op_kind == OP_COPY ? "Copy" : a->op_kind == OP_MOVE ? "Move" : "Rename";
    int i, done = 0, failed = 0, skipped = 0, dnode, stop = 0, nodes[64], nn = 0, across = 0;
    unsigned long blocks = 0;

    if (a->op_kind == OP_RENAME) {
        /* in place: the destination is each file's own directory */
        char sdev[VT_SPEC_MAX];
        vt_tree_spec(&a->t, a->op_src[0].node, sdev, sizeof sdev, dir, sizeof dir);
        vt_strlcpy(dev, sdev, sizeof dev);
        for (i = 1; i < a->op_n; i++) {
            char d2[VT_SPEC_MAX], v2[VT_SPEC_MAX];
            vt_tree_spec(&a->t, a->op_src[i].node, d2, sizeof d2, v2, sizeof v2);
            if (vt_strcasecmp(v2, dir) || vt_strcasecmp(d2, dev)) {
                vt_set_msg(a, 1, "Rename works in one directory at a time (the tagged files are in several)");
                return;
            }
        }
    } else if (!destination(a, dest_spec, dev, sizeof dev, dir, sizeof dir)) return;

    if (a->op_kind != OP_COPY) {
        char sdev[VT_SPEC_MAX], sdir[VT_SPEC_MAX];
        vt_tree_spec(&a->t, a->op_src[0].node, sdev, sizeof sdev, sdir, sizeof sdir);
        if (!same_dev(sdev, dev)) {
            /* Another disk: copy, compare block by block, and only then delete the
               original. The copy is a new version there, as with Copy. */
            if (a->op_kind != OP_MOVE) { vt_set_msg(a, 1, "Rename stays on one disk"); return; }
            across = 1;
        }
    }
    if (!plan(a, a->op_pattern, dev, dir, a->op_kind == OP_COPY || across)) return;
    for (i = 0; i < a->op_n; i++) blocks += a->op_src[i].blocks;

    if (across) {
        char b[32];
        snprintf(q, sizeof q, "Move %d file%s (%s blocks) to another disk, %s%s: copy, compare, then "
                 "delete each original?  Y yes  N no", a->op_n, a->op_n == 1 ? "" : "s",
                 vt_commas(blocks, b, sizeof b), dev, dir);
        if (vt_ask(a, q, "YN", 'N') != 'Y') { vt_set_msg(a, 0, "Nothing done"); return; }
    } else if (a->op_n > 1) {
        char b[32];
        snprintf(q, sizeof q, "%s %d files (%s blocks) %s %s%s?  Y yes  N no", verb, a->op_n,
                 vt_commas(blocks, b, sizeof b), a->op_kind == OP_RENAME ? "in" : "to", dev, dir);
        if (vt_ask(a, q, "YN", 'N') != 'Y') { vt_set_msg(a, 0, "Nothing done"); return; }
    }

    for (i = 0; i < a->op_n && !stop; i++) {
        vt_src *s = &a->op_src[i];
        char to[3 * VT_SPEC_MAX], res[VT_SPEC_MAX], m[200];
        unsigned int st;
        if (across) {
            snprintf(to, sizeof to, "%s%s%s%s", dev, dir, s->oname, s->otype);
            st = vtp_copy(s->path, to, res, sizeof res);
            vt_log_op(a, "COPY", s->path, (st & 1) ? res : to, st);
            if (st & 1) {
                st = vtp_compare(s->path, res);
                vt_log_op(a, "CHECK", res, 0, st);
                if (st & 1) {
                    st = vtp_delete(s->path);
                    vt_log_op(a, "DELETE", s->path, 0, st);
                } else st = VTP_DIFFERENT;       /* the copy stays; the original too */
            }
            if (st & 1) { done++; if (nn < 64) nodes[nn++] = s->node; continue; }
            goto failed_one;
        } else if (a->op_kind == OP_COPY) {
            snprintf(to, sizeof to, "%s%s%s%s", dev, dir, s->oname, s->otype);
            st = vtp_copy(s->path, to, res, sizeof res);
        } else {
            snprintf(to, sizeof to, "%s%s%s%s;%u", dev, dir, s->oname, s->otype, s->ver);
            if (vt_strcasecmp(to, s->path) == 0) { skipped++; continue; }    /* already there */
            st = vtp_rename(s->path, to, res, sizeof res);
        }
        vt_log_op(a, a->op_kind == OP_COPY ? "COPY" : a->op_kind == OP_MOVE ? "MOVE" : "RENAME",
               s->path, (st & 1) ? res : to, st);
        if (st & 1) {
            done++;
            if (a->op_kind == OP_COPY) untag(a, s);
            if (nn < 64) nodes[nn++] = s->node;
            continue;
        }
    failed_one:
        failed++;
        if (st == VTP_DIFFERENT) vt_strlcpy(m, "the copy differs from the original: original kept", sizeof m);
        else vtp_message(st, m, sizeof m);
        if (i + 1 < a->op_n) {
            snprintf(q, sizeof q, "%s%s;%u: %.80s   S skip it  Q stop", s->name, s->type, s->ver, m);
            if (vt_ask(a, q, "SQ", 'Q') != 'S') stop = 1;
        } else vt_set_msg(a, 1, "%s%s;%u: %s", s->name, s->type, s->ver, m);
    }

    /* what changed, read again */
    dnode = vt_find_node(a, dev, dir);
    vt_refresh(a, dnode);
    for (i = 0; i < nn; i++) {
        int j, seen = 0;
        for (j = 0; j < i; j++) if (nodes[j] == nodes[i]) seen = 1;
        if (!seen && nodes[i] != dnode && a->op_kind != OP_COPY) vt_refresh(a, nodes[i]);
    }
    if (!failed || done)
        vt_set_msg(a, failed != 0, "%s: %d done%s%s%s", verb, done,
                   failed ? ", failed: " : "", failed ? "see the log" : "",
                   skipped ? " (some were already there)" : "");
    if (stop) vt_set_msg(a, 1, "%s stopped after %d of %d; the log has the details", verb, done, a->op_n);
    free_op(a);
}

void vt_op_prompt_done(vt_app *a, int kind, const char *text)
{
    char def[VT_SPEC_MAX];
    switch (kind) {
    case P_COPY_AS:
        vt_strlcpy(a->op_pattern, text, sizeof a->op_pattern);
        current_dir(a, def, sizeof def);
        vt_start_prompt(a, P_COPY_TO, "to: ", def);
        return;
    case P_COPY_TO: case P_MOVE_TO:
        run(a, text);
        return;
    case P_RENAME:
        vt_strlcpy(a->op_pattern, text, sizeof a->op_pattern);
        run(a, 0);
        return;
    case P_MKDIR: {
        char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], nd[VT_SPEC_MAX];
        int node = vt_cur_node(a), child;
        unsigned int st;
        if (!vt_valid_dirname(text)) {
            vt_set_msg(a, 1, "Not a directory name: %s (letters, digits, $ _ -, up to 39)", text);
            return;
        }
        vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
        if (!vt_dir_child(dir, text, nd, sizeof nd)) { vt_set_msg(a, 1, "Name too long"); return; }
        st = vtp_mkdir(dev, nd);
        {
            char full[2 * VT_SPEC_MAX];
            snprintf(full, sizeof full, "%s%s", dev, nd);
            vt_log_op(a, "MKDIR", full, 0, st);
        }
        if (st == VTP_OK) { vt_set_msg(a, 1, "%s%s exists already", dev, nd); return; }
        if (!(st & 1)) { vt_status_msg(a, nd, st); return; }
        a->t.n[node].flags &= (unsigned char) ~VT_N_SCANNED;
        vt_scan_node(a, node);
        vt_refresh(a, node);
        a->t.n[node].flags |= VT_N_OPEN;
        child = vt_tree_find_child(&a->t, node, text);
        vt_rebuild_vis(a);
        if (child >= 0) vt_set_cursor_node(a, child);
        vt_set_msg(a, 0, "Created %s%s", dev, nd);
        return;
    }
    case P_RENDIR: {
        char dev[VT_SPEC_MAX], pdir[VT_SPEC_MAX], from[3 * VT_SPEC_MAX], to[3 * VT_SPEC_MAX], res[VT_SPEC_MAX];
        int node = vt_cur_node(a), parent = a->t.n[node].parent;
        unsigned int st;
        if (!vt_valid_dirname(text)) {
            vt_set_msg(a, 1, "Not a directory name: %s (letters, digits, $ _ -, up to 39)", text);
            return;
        }
        if (vt_strcasecmp(text, a->t.n[node].name) == 0) return;
        if (vt_tree_find_child(&a->t, parent, text) >= 0) { vt_set_msg(a, 1, "%s exists already", text); return; }
        vt_tree_spec(&a->t, parent, dev, sizeof dev, pdir, sizeof pdir);
        snprintf(from, sizeof from, "%s%s%s.DIR;1", dev, pdir, a->t.n[node].name);
        snprintf(to, sizeof to, "%s%s%s.DIR;1", dev, pdir, text);
        {
            /* A new directory has no delete access, and a rename removes the old
               entry. If the user owns it, offer to allow D just for the rename and
               put the protection back afterwards (D10). */
            vt_flist *fl;
            int i, restore = 0;
            unsigned short prot = 0;
            if (!a->t.n[parent].files.loaded) vt_load_files(a, parent);
            fl = &a->t.n[parent].files;
            for (i = 0; i < fl->n; i++)
                if ((fl->f[i].flags & VT_F_DIR) && fl->f[i].ver == 1
                    && !vt_strcasecmp(fl->f[i].name, a->t.n[node].name)) break;
            if (i < fl->n && (fl->f[i].a.status & 1) && (fl->f[i].a.prot & 0x0080)) {
                char q[VT_SPEC_MAX + 160], ps[48];
                prot = fl->f[i].a.prot;
                if (fl->f[i].a.uic != a->sess.uic) {
                    vt_set_msg(a, 1, "%s.DIR has no delete access, which renaming needs, and isn't yours",
                               a->t.n[node].name);
                    return;
                }
                snprintf(q, sizeof q, "%s.DIR %s has no delete access, which renaming needs. "
                         "Allow it for the rename only?  Y yes  N no", a->t.n[node].name,
                         vt_fmt_prot(prot, ps, sizeof ps));
                if (vt_ask(a, q, "YN", 'N') != 'Y') { vt_set_msg(a, 0, "Nothing done"); return; }
                st = vtp_set_prot(from, (unsigned short) (prot & ~0x0080));
                vt_log_op(a, "PROT", from, 0, st);
                if (!(st & 1)) { vt_status_msg(a, from, st); return; }
                restore = 1;
            }
            st = vtp_rename(from, to, res, sizeof res);
            vt_log_op(a, "RENAME", from, (st & 1) ? res : to, st);
            if (restore) {                          /* back as it was, wherever it now is */
                unsigned int pst = vtp_set_prot((st & 1) ? res : from, prot);
                vt_log_op(a, "PROT", (st & 1) ? res : from, 0, pst);
                if (!(pst & 1)) { vt_status_msg(a, "Putting the protection back", pst); return; }
            }
        }
        if (!(st & 1)) { vt_status_msg(a, a->t.n[node].name, st); return; }
        vt_tree_rename(&a->t, node, text);          /* every path below follows the name */
        vt_refresh(a, parent);
        vt_rebuild_vis(a);
        vt_set_cursor_node(a, node);
        vt_set_msg(a, 0, "Renamed to %s", text);
        return;
    }
    default: return;
    }
}

/* ---------- directories ---------- */

void vt_op_mkdir(vt_app *a)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], label[64];
    int node = vt_cur_node(a);
    if (node < 0) return;
    vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
    snprintf(label, sizeof label, "Make directory in %.30s: ", dir);
    vt_start_prompt(a, P_MKDIR, label, "");
}

void vt_op_rendir(vt_app *a)
{
    int node = vt_cur_node(a);
    if (node < 0) return;
    if (a->t.n[node].parent < 0) { vt_set_msg(a, 1, "A disk can't be renamed"); return; }
    if (a->t.n[node].flags & VT_N_CLOSED) {
        vt_set_msg(a, 1, "%s is an alias or unchecked entry: VTREE won't rename it", a->t.n[node].name);
        return;
    }
    vt_start_prompt(a, P_RENDIR, "Rename directory to: ", a->t.n[node].name);
}

/* ---------- edit and subprocess ---------- */

void vt_op_edit(vt_app *a, const vt_ref *r)
{
    char path[VT_SPEC_MAX];
    int node = r->node;
    unsigned int st;
    vt_ref_path(a, r, path, sizeof path);
    st = vtp_spawn(path);
    if (!(st & 1)) vt_status_msg(a, "Editor", st);
    vt_refresh(a, node);                       /* a new version, perhaps */
}

void vt_op_exec(vt_app *a)
{
    unsigned int st = vtp_spawn(0);
    if (!(st & 1)) vt_status_msg(a, "Subprocess", st);
    if (a->list_node >= 0) vt_refresh(a, a->list_node);
}

/* ---------- pointing at the destination in the tree ---------- */

void vt_pick_start(vt_app *a)
{
    a->pick_save.mode = a->mode; a->pick_save.kind = a->kind;
    a->pick_save.list_node = a->list_node; a->pick_save.fcur = a->fcur;
    a->pick_save.ftop = a->ftop; a->pick_save.tcur = a->tcur; a->pick_save.ttop = a->ttop;
    a->picking = 1;
    a->mode = M_TREE;
}

static void pick_end(vt_app *a)
{
    int tnode = a->pick_save.tcur < a->nvis ? a->vis[a->pick_save.tcur] : -1;
    a->picking = 0;
    a->mode = a->pick_save.mode; a->kind = a->pick_save.kind;
    a->list_node = a->pick_save.list_node; a->fcur = a->pick_save.fcur;
    a->ftop = a->pick_save.ftop; a->tcur = a->pick_save.tcur; a->ttop = a->pick_save.ttop;
    (void) tnode;
    a->refs_dirty = 1;
    a->pending_load = 0;
}

/* 1: handled here; 0: a movement key for the tree; -1: not allowed while pointing. */
int vt_pick_key(vt_app *a, int key)
{
    switch (key) {
    case VK_RETURN: case VK_ENTER: case VK_SELECT: {
        char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], both[2 * VT_SPEC_MAX];
        vt_tree_spec(&a->t, vt_cur_node(a), dev, sizeof dev, dir, sizeof dir);
        snprintf(both, sizeof both, "%s%s", dev, dir);
        vt_edit_init(&a->edit, both);
        pick_end(a);
        return 1;
    }
    case VK_F10: case VK_CTRL_Z:
        pick_end(a);
        return 1;
    default:
        break;
    }
    {   /* moving and opening only; no other tree command while pointing */
        static const int ok[] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_PREV, VK_NEXT, '+', '-', '*',
                                  VK_PF1, 0 };
        int i;
        for (i = 0; ok[i]; i++) if (ok[i] == key) return 0;
        return -1;
    }
}

vt_file *vt_dir_entry(vt_app *a, int node)
{
    int parent = a->t.n[node].parent, i;
    vt_flist *fl;
    if (parent < 0) return 0;
    vt_load_files(a, parent);
    fl = &a->t.n[parent].files;
    for (i = 0; i < fl->n; i++)
        if ((fl->f[i].flags & VT_F_DIR) && fl->f[i].ver == 1
            && !vt_strcasecmp(fl->f[i].name, a->t.n[node].name))
            return &fl->f[i];
    return 0;
}
