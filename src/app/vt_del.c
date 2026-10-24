/* vt_del.c - Phase 3: delete, purge, delete an empty directory, prune a branch, and
   graft (move a branch within a disk). D10's rules, in order, for each of them:
   read the directories again and decide from that fresh reading, never from what
   was on the screen; refuse anything an alias or unchecked entry touches (D8); show
   what will go and ask (default No; a prune also wants the name typed back); delete
   exact versions only; log every change; on a failure ask skip or stop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vt_app.h"
#include "vt_util.h"
#include "vt_plan.h"

/* ---------- typed confirmation ---------- */

int vt_ask_text(vt_app *a, const char *question, char *out, int size)
{
    int idle = 0, r = VT_EDIT_MORE, k;
    int saved = a->prompt;
    char label[sizeof a->prompt_label];
    vt_edit saved_edit = a->edit;
    vt_strlcpy(label, a->prompt_label, sizeof label);
    a->prompt = P_ASKTEXT;
    vt_strlcpy(a->prompt_label, question, sizeof a->prompt_label);
    vt_edit_init(&a->edit, "");
    vtp_flush_keys();
    while (r == VT_EDIT_MORE) {
        vt_app_draw(a);
        vtp_draw(&a->scr);
        k = vtp_key(30);
        if (k == VK_TIMEOUT) { if (++idle > 1000) { r = VT_EDIT_CANCEL; break; } continue; }
        r = vt_edit_key(&a->edit, k);
    }
    vt_strlcpy(out, r == VT_EDIT_DONE ? a->edit.buf : "", (size_t) size);
    a->prompt = saved;
    a->edit = saved_edit;
    vt_strlcpy(a->prompt_label, label, sizeof a->prompt_label);
    return r == VT_EDIT_DONE;
}

/* ---------- the preview page ---------- */

/* What a delete will remove. For a directory file that lacks delete access, needd
   is set and prot holds its protection, so D is added just before its delete and
   the old protection goes back if the delete fails. */
typedef struct {
    char **path; int n, cap;
    unsigned short *prot;
    unsigned char *needd;
    unsigned long blocks;
} vt_victims;

static void victims_free(vt_victims *v)
{
    int i;
    for (i = 0; i < v->n; i++) free(v->path[i]);
    free(v->path); free(v->prot); free(v->needd);
    memset(v, 0, sizeof *v);
}

static int victims_add_p(vt_victims *v, const char *path, unsigned long blocks, int needd, unsigned short prot)
{
    size_t n = strlen(path) + 1;
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 64;
        char **np = (char **) realloc(v->path, (size_t) cap * sizeof *np);
        unsigned short *pp;
        unsigned char *nd;
        if (!np) return 0;
        v->path = np;
        pp = (unsigned short *) realloc(v->prot, (size_t) cap * sizeof *pp);
        if (!pp) return 0;
        v->prot = pp;
        nd = (unsigned char *) realloc(v->needd, (size_t) cap);
        if (!nd) return 0;
        v->needd = nd;
        v->cap = cap;
    }
    if (!(v->path[v->n] = (char *) malloc(n))) return 0;
    memcpy(v->path[v->n], path, n);
    v->prot[v->n] = prot;
    v->needd[v->n] = (unsigned char) (needd != 0);
    v->n++;
    v->blocks += blocks;
    return 1;
}

static int victims_add(vt_victims *v, const char *path, unsigned long blocks)
{
    return victims_add_p(v, path, blocks, 0, 0);
}

/* Show what will be deleted on a page, ask, and put the screen back. */
static int confirm_page(vt_app *a, const char *title, const vt_victims *v, int dirs,
                        const char *question)
{
    int mode = a->mode, back = a->back_mode, i, shown, ans;
    char line[VT_SPEC_MAX + 64], b[32];
    vt_text_free(a);
    vt_text_add(a, title);
    vt_text_add(a, "");
    if (dirs) snprintf(line, sizeof line, "%d file%s (%s blocks) and %d director%s", v->n - dirs,
                       v->n - dirs == 1 ? "" : "s", vt_commas(v->blocks, b, sizeof b), dirs,
                       dirs == 1 ? "y" : "ies");
    else snprintf(line, sizeof line, "%d file%s, %s block%s", v->n, v->n == 1 ? "" : "s",
                  vt_commas(v->blocks, b, sizeof b), v->blocks == 1 ? "" : "s");
    vt_text_add(a, line);
    vt_text_add(a, "");
    shown = a->rows - 10 < 3 ? 3 : a->rows - 10;
    for (i = 0; i < v->n && i < shown; i++) {
        snprintf(line, sizeof line, "  %s", v->path[i]);
        vt_text_add(a, line);
    }
    if (v->n > shown) {
        snprintf(line, sizeof line, "  ... and %d more", v->n - shown);
        vt_text_add(a, line);
    }
    a->back_mode = mode;
    a->mode = M_INFO;
    vt_strlcpy(a->text_title, "Check what will be deleted", sizeof a->text_title);
    ans = vt_ask(a, question, "YN", 'N');
    a->text_title[0] = '\0';
    vt_text_free(a);
    a->mode = mode;
    a->back_mode = back;
    return ans == 'Y';
}

/* Delete the list in order; failures ask skip or stop. Returns how many went. */
static int delete_all(vt_app *a, const vt_victims *v, int *failed)
{
    int i, done = 0;
    *failed = 0;
    for (i = 0; i < v->n; i++) {
        unsigned int st = vtp_delete(v->path[i]);
        vt_log_op(a, "DELETE", v->path[i], 0, st);
        if (st & 1) { done++; continue; }
        (*failed)++;
        if (i + 1 < v->n) {
            char m[200], q[VT_SPEC_MAX + 260];
            vtp_message(st, m, sizeof m);
            snprintf(q, sizeof q, "%.60s: %.80s   S skip it  Q stop", v->path[i], m);
            if (vt_ask(a, q, "SQ", 'Q') != 'S') { vt_set_msg(a, 1, "Stopped after %d of %d; the log has the details", done, v->n); return done; }
        } else {
            char m[200];
            vtp_message(st, m, sizeof m);
            vt_set_msg(a, 1, "%s: %s", v->path[i], m);
        }
    }
    return done;
}

static void file_path(vt_app *a, int node, const vt_file *f, char *out, int size)
{
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
    snprintf(out, (size_t) size, "%s%s%s%s;%u", dev, dir, f->name, f->type, f->ver);
}

/* ---------- delete files ---------- */

void vt_op_delete(vt_app *a, int tagged)
{
    vt_victims v;
    int i, n, failed, done, nodes[64], nn = 0;
    char q[160];
    n = vt_collect(a, tagged);
    if (!n) { vt_set_msg(a, 1, tagged ? "No files are tagged in this list" : "No file here"); return; }
    /* fresh: read each directory again, and find every file again by its file ID */
    for (i = 0; i < a->op_n; i++) {
        int j, seen = 0;
        for (j = 0; j < nn; j++) if (nodes[j] == a->op_src[i].node) seen = 1;
        if (!seen && nn < 64) { nodes[nn++] = a->op_src[i].node; vt_load_files(a, a->op_src[i].node); }
    }
    memset(&v, 0, sizeof v);
    for (i = 0; i < a->op_n; i++) {
        vt_flist *fl = &a->t.n[a->op_src[i].node].files;
        int j;
        for (j = 0; j < fl->n; j++)
            if (!memcmp(fl->f[j].fid, a->op_src[i].fid, 6) && !(fl->f[j].flags & VT_F_DIR)) {
                char path[VT_SPEC_MAX];
                file_path(a, a->op_src[i].node, &fl->f[j], path, sizeof path);
                victims_add(&v, path, fl->f[j].a.eof_blocks);
                break;
            }
    }
    if (!v.n) { vt_set_msg(a, 1, "The file%s no longer there", n == 1 ? " is" : "s are"); victims_free(&v); return; }
    if (v.n < n) vt_set_msg(a, 1, "%d of the files are no longer there", n - v.n);
    snprintf(q, sizeof q, "Delete %s?  Y yes  N no", v.n == 1 ? "this file" : "these files");
    if (!confirm_page(a, "DELETE", &v, 0, q)) { vt_set_msg(a, 0, "Nothing deleted"); victims_free(&v); return; }
    done = delete_all(a, &v, &failed);
    for (i = 0; i < nn; i++) vt_refresh(a, nodes[i]);
    if (!failed) vt_set_msg(a, 0, "Deleted %d file%s", done, done == 1 ? "" : "s");
    else if (a->msg[0] == '\0') vt_set_msg(a, 1, "Deleted %d; %d failed (see the log)", done, failed);
    victims_free(&v);
}

/* ---------- purge ---------- */

static int purge_keep = 1;

void vt_op_purge(vt_app *a)
{
    char def[8];
    snprintf(def, sizeof def, "%d", purge_keep);
    vt_start_prompt(a, P_PURGE, "Purge: how many versions of each file to keep? ", def);
}

void vt_op_purge_done(vt_app *a, const char *text)
{
    int keep = atoi(text), node, i, failed, done;
    int *nodes = 0, nn = 0;
    vt_victims v;
    char q[200], title[2 * VT_SPEC_MAX];
    if (keep < 1 || keep > 32767) { vt_set_msg(a, 1, "Keep at least 1 version (1 to 32767)"); return; }
    purge_keep = keep;
    /* the directories: the tree's current one, or every one in the file list */
    nodes = (int *) malloc((size_t) (a->t.count + 1) * sizeof *nodes);
    if (!nodes) return;
    if (a->mode == M_TREE) nodes[nn++] = vt_cur_node(a);
    else {
        if (a->refs_dirty) vt_rebuild_refs(a);
        for (i = 0; i < a->nrefs; i++) {
            int j, seen = 0;
            for (j = 0; j < nn; j++) if (nodes[j] == a->refs[i].node) seen = 1;
            if (!seen) nodes[nn++] = a->refs[i].node;
        }
    }
    memset(&v, 0, sizeof v);
    for (i = 0; i < nn; i++) {
        vt_flist *fl;
        vt_nver *nv;
        int *out, j, m = 0;
        node = nodes[i];
        vt_load_files(a, node);                     /* fresh */
        fl = &a->t.n[node].files;
        if (!(fl->status & 1) || !fl->complete) {
            vt_set_msg(a, 1, "%s could not be read completely: purge refused", a->t.n[node].name);
            victims_free(&v); free(nodes); return;
        }
        nv = (vt_nver *) malloc((size_t) (fl->n + 1) * sizeof *nv);
        out = (int *) malloc((size_t) (fl->n + 1) * sizeof *out);
        if (!nv || !out) { free(nv); free(out); victims_free(&v); free(nodes); return; }
        for (j = 0; j < fl->n; j++) {
            if ((fl->f[j].flags & VT_F_DIR) || !vt_filter_match_nt(&a->filter, fl->f[j].name, fl->f[j].type)) {
                nv[j].name = ""; nv[j].type = ""; nv[j].ver = 0;   /* not a candidate */
                continue;
            }
            nv[j].name = fl->f[j].name; nv[j].type = fl->f[j].type; nv[j].ver = fl->f[j].ver;
            m++;
        }
        if (m) {
            vt_purge_select(nv, fl->n, keep, out);
            for (j = 0; j < fl->n; j++)
                if (out[j] && nv[j].ver) {
                    char path[VT_SPEC_MAX];
                    file_path(a, node, &fl->f[j], path, sizeof path);
                    victims_add(&v, path, fl->f[j].a.eof_blocks);
                }
        }
        free(nv); free(out);
    }
    if (!v.n) { vt_set_msg(a, 0, "Nothing to purge: no file has more than %d version%s", keep, keep == 1 ? "" : "s"); free(nodes); return; }
    snprintf(title, sizeof title, "PURGE, keeping %d version%s of each %s", keep, keep == 1 ? "" : "s", a->filter.text);
    snprintf(q, sizeof q, "Delete these older versions?  Y yes  N no");
    if (!confirm_page(a, title, &v, 0, q)) { vt_set_msg(a, 0, "Nothing deleted"); victims_free(&v); free(nodes); return; }
    done = delete_all(a, &v, &failed);
    for (i = 0; i < nn; i++) vt_refresh(a, nodes[i]);
    if (!failed) vt_set_msg(a, 0, "Purged %d old version%s", done, done == 1 ? "" : "s");
    victims_free(&v);
    free(nodes);
}

/* ---------- the tree after a fresh reading ---------- */

/* Drop tree children that are no longer in the directory's fresh listing. */
void vt_sync_children(vt_app *a, int node)
{
    int c = a->t.n[node].child;
    vt_flist *fl = &a->t.n[node].files;
    if (!fl->loaded || !(fl->status & 1) || !fl->complete) return;
    while (c >= 0) {
        int next = a->t.n[c].next, i, found = 0;
        for (i = 0; i < fl->n; i++)
            if ((fl->f[i].flags & VT_F_DIR) && fl->f[i].ver == 1 && !vt_strcasecmp(fl->f[i].name, a->t.n[c].name))
                found = 1;
        if (!found) vt_tree_remove(&a->t, c);
        c = next;
    }
}

/* A directory's own .DIR entry needs D to be deleted (or renamed, N11). If it lacks
   it and the user owns it, ask once (the caller's question covers it) and allow it.
   Returns 1 if deletable as far as protection goes, with *restore set when the old
   protection must go back on failure. */
static int allow_delete(vt_app *a, const vt_file *e, const char *path, unsigned short *restore, int *changed)
{
    unsigned int st;
    *changed = 0;
    if (!(e->a.status & 1) || !(e->a.prot & 0x0080)) return 1;
    if (e->a.uic != a->sess.uic) return 0;
    *restore = e->a.prot;
    st = vtp_set_prot(path, (unsigned short) (e->a.prot & ~0x0080));
    vt_log_op(a, "PROT", path, 0, st);
    if (!(st & 1)) return 0;
    *changed = 1;
    return 1;
}

/* ---------- delete an empty directory ---------- */

void vt_op_rmdir(vt_app *a)
{
    int node = vt_cur_node(a), parent;
    vt_file *e;
    char dev[VT_SPEC_MAX], pdir[VT_SPEC_MAX], path[3 * VT_SPEC_MAX], q[VT_SPEC_MAX + 200];
    unsigned short old = 0;
    int changed;
    unsigned int st;
    if (node < 0) return;
    parent = a->t.n[node].parent;
    if (parent < 0) { vt_set_msg(a, 1, "A disk can't be deleted"); return; }
    if (a->t.n[node].flags & VT_N_CLOSED) {
        vt_set_msg(a, 1, "%s is an alias or unchecked entry: VTREE never deletes one (it could take another tree with it)",
                   a->t.n[node].name);
        return;
    }
    vt_load_files(a, node);                          /* fresh: is it really empty? */
    if (!(a->t.n[node].files.status & 1)) { vt_status_msg(a, a->t.n[node].name, a->t.n[node].files.status); return; }
    vt_sync_children(a, node);
    if (a->t.n[node].files.n) {
        vt_set_msg(a, 1, "%s is not empty (%d entries): Prune (GOLD P) deletes a branch", a->t.n[node].name,
                   a->t.n[node].files.n);
        vt_rebuild_vis(a);
        return;
    }
    e = vt_dir_entry(a, node);
    if (!e) { vt_set_msg(a, 1, "%s.DIR;1 is not in its parent's listing", a->t.n[node].name); return; }
    if ((e->flags & VT_F_ALIAS)) { vt_set_msg(a, 1, "%s is an alias: refused", a->t.n[node].name); return; }
    vt_tree_spec(&a->t, parent, dev, sizeof dev, pdir, sizeof pdir);
    snprintf(path, sizeof path, "%s%s%s.DIR;1", dev, pdir, a->t.n[node].name);
    if ((e->a.prot & 0x0080) && e->a.uic != a->sess.uic) {
        vt_set_msg(a, 1, "%s.DIR has no delete access and isn't yours", a->t.n[node].name);
        return;
    }
    snprintf(q, sizeof q, "Delete the empty directory %s%s?%s  Y yes  N no", pdir, a->t.n[node].name,
             (e->a.prot & 0x0080) ? " (allowing the delete access it lacks)" : "");
    if (vt_ask(a, q, "YN", 'N') != 'Y') { vt_set_msg(a, 0, "Nothing deleted"); return; }
    if (!allow_delete(a, e, path, &old, &changed)) { vt_set_msg(a, 1, "Can't allow delete access on %s", path); return; }
    st = vtp_delete(path);
    vt_log_op(a, "DELETE", path, 0, st);
    if (!(st & 1)) {
        if (changed) vt_log_op(a, "PROT", path, 0, vtp_set_prot(path, old));
        vt_status_msg(a, a->t.n[node].name, st);
        return;
    }
    vt_tree_remove(&a->t, node);
    vt_refresh(a, parent);
    vt_rebuild_vis(a);
    vt_set_cursor_node(a, parent);
    vt_set_msg(a, 0, "Deleted %s", path);
}

/* ---------- prune: delete a branch ---------- */

void vt_op_prune(vt_app *a)
{
    int node = vt_cur_node(a), parent, *order = 0, no = 0, cap, i, j, dirs = 0, failed, done;
    int *stack = 0, sp = 0, need_d = 0;
    vt_victims v;
    char q[VT_SPEC_MAX + 200], title[3 * VT_SPEC_MAX], typed[VT_NAME_MAX], dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
    if (node < 0) return;
    parent = a->t.n[node].parent;
    if (parent < 0) { vt_set_msg(a, 1, "A whole disk can't be pruned"); return; }
    if (a->t.n[node].flags & VT_N_CLOSED) {
        vt_set_msg(a, 1, "%s is an alias or unchecked entry: VTREE never prunes one", a->t.n[node].name);
        return;
    }
    cap = a->t.count + 64;
    order = (int *) malloc((size_t) cap * sizeof *order);
    stack = (int *) malloc((size_t) cap * sizeof *stack);
    if (!order || !stack) { free(order); free(stack); return; }
    memset(&v, 0, sizeof v);

    /* 1. Read the whole branch again; refuse on anything unreadable or any alias. */
    vt_progress(a, "Reading the branch to prune...");
    stack[sp++] = node;
    while (sp) {
        int n = stack[--sp], c;
        vt_flist *fl;
        a->t.n[n].flags &= (unsigned char) ~VT_N_SCANNED;
        vt_load_files(a, n);
        fl = &a->t.n[n].files;
        if (!(fl->status & 1) || !fl->complete) {
            vt_tree_spec(&a->t, n, dev, sizeof dev, dir, sizeof dir);
            vt_set_msg(a, 1, "Prune refused, nothing deleted: %s can't be read completely", dir);
            goto out;
        }
        vt_sync_children(a, n);
        for (i = 0; i < fl->n; i++)
            if (fl->f[i].flags & VT_F_ALIAS) {
                vt_tree_spec(&a->t, n, dev, sizeof dev, dir, sizeof dir);
                vt_set_msg(a, 1, "Prune refused, nothing deleted: %s%s.DIR is an alias or unchecked entry (D8)",
                           dir, fl->f[i].name);
                goto out;
            }
        if (no + 1 >= cap || sp + 64 >= cap) {
            int nc = cap * 2, *o2 = (int *) realloc(order, (size_t) nc * sizeof *o2), *s2;
            if (!o2) goto out;
            order = o2;
            s2 = (int *) realloc(stack, (size_t) nc * sizeof *s2);
            if (!s2) goto out;
            stack = s2; cap = nc;
        }
        order[no++] = n;
        for (c = a->t.n[n].child; c >= 0; c = a->t.n[c].next) {
            if (a->t.n[c].flags & VT_N_CLOSED) {
                vt_set_msg(a, 1, "Prune refused, nothing deleted: %s is an alias or unchecked entry (D8)", a->t.n[c].name);
                goto out;
            }
            if (sp + 1 >= cap) goto out;
            stack[sp++] = c;
        }
    }

    /* 2. What will go, deepest first: each directory's files, then the directory. */
    for (i = no - 1; i >= 0; i--) {
        int n = order[i];
        vt_flist *fl = &a->t.n[n].files;
        for (j = 0; j < fl->n; j++) {
            char path[VT_SPEC_MAX];
            if (fl->f[j].flags & VT_F_DIR) continue;     /* its subdirectory comes as a node */
            file_path(a, n, &fl->f[j], path, sizeof path);
            if (!victims_add(&v, path, fl->f[j].a.eof_blocks)) goto out;
        }
        {
            int p = a->t.n[n].parent, k;
            vt_flist *pf = &a->t.n[p].files;
            char pdev[VT_SPEC_MAX], pdir[VT_SPEC_MAX], path[3 * VT_SPEC_MAX];
            if (!pf->loaded) vt_load_files(a, p);
            for (k = 0; k < pf->n; k++)
                if ((pf->f[k].flags & VT_F_DIR) && pf->f[k].ver == 1 && !vt_strcasecmp(pf->f[k].name, a->t.n[n].name))
                    break;
            if (k == pf->n) { vt_set_msg(a, 1, "Prune refused: %s.DIR not found in its parent", a->t.n[n].name); goto out; }
            if (pf->f[k].a.prot & 0x0080) {
                if (pf->f[k].a.uic != a->sess.uic) {
                    vt_set_msg(a, 1, "Prune refused, nothing deleted: %s.DIR has no delete access and isn't yours",
                               a->t.n[n].name);
                    goto out;
                }
                need_d++;
            }
            vt_tree_spec(&a->t, p, pdev, sizeof pdev, pdir, sizeof pdir);
            snprintf(path, sizeof path, "%s%s%s.DIR;1", pdev, pdir, a->t.n[n].name);
            if (!victims_add_p(&v, path, pf->f[k].a.eof_blocks, (pf->f[k].a.prot & 0x0080) != 0,
                               pf->f[k].a.prot))
                goto out;
            dirs++;
        }
    }
    vt_rebuild_vis(a);

    /* 3. Show it, ask, and have the name typed back. */
    vt_tree_spec(&a->t, node, dev, sizeof dev, dir, sizeof dir);
    snprintf(title, sizeof title, "PRUNE %s%s", dev, dir);
    snprintf(q, sizeof q, "Delete this whole branch%s?  Y yes  N no",
             need_d ? " (allowing delete access on its directories)" : "");
    if (!confirm_page(a, title, &v, dirs, q)) { vt_set_msg(a, 0, "Nothing deleted"); goto out; }
    snprintf(q, sizeof q, "To prune it, type its name (%s) and Return: ", a->t.n[node].name);
    if (!vt_ask_text(a, q, typed, sizeof typed) || vt_strcasecmp(typed, a->t.n[node].name)) {
        vt_set_msg(a, 0, "The name was not typed: nothing deleted");
        goto out;
    }

    /* 4. Delete, deepest first. Directory files get D (ours, agreed) just before. */
    {
        int k;
        done = 0; failed = 0;
        for (k = 0; k < v.n; k++) {
            const char *pth = v.path[k];
            unsigned int st;
            if (v.needd[k]) {                       /* owner D, nothing else changed */
                st = vtp_set_prot(pth, (unsigned short) (v.prot[k] & ~0x0080));
                vt_log_op(a, "PROT", pth, 0, st);
            }
            st = vtp_delete(pth);
            vt_log_op(a, "DELETE", pth, 0, st);
            if (!(st & 1) && v.needd[k]) vt_log_op(a, "PROT", pth, 0, vtp_set_prot(pth, v.prot[k]));
            if (st & 1) { done++; continue; }
            failed++;
            {
                char m[200], qq[VT_SPEC_MAX + 260];
                vtp_message(st, m, sizeof m);
                snprintf(qq, sizeof qq, "%.60s: %.80s   S skip it  Q stop", pth, m);
                if (k + 1 < v.n && vt_ask(a, qq, "SQ", 'Q') != 'S') break;
            }
        }
    }

    /* 5. The tree, from a fresh reading. */
    if (!failed) {
        vt_tree_remove(&a->t, node);
        vt_refresh(a, parent);
        vt_rebuild_vis(a);
        vt_set_cursor_node(a, parent);
        vt_set_msg(a, 0, "Pruned %s%s: %d files and directories deleted", dev, dir, done);
    } else {
        for (i = 0; i < no; i++)
            if (!(a->t.n[order[i]].flags & VT_N_DEAD)) { vt_load_files(a, order[i]); vt_sync_children(a, order[i]); }
        vt_refresh(a, parent);
        vt_sync_children(a, parent);
        vt_rebuild_vis(a);
        vt_set_msg(a, 1, "Prune incomplete: %d deleted, %d failed (see the log)", done, failed);
    }
out:
    victims_free(&v);
    free(order); free(stack);
}

/* ---------- graft: move a branch within a disk ---------- */

void vt_op_graft(vt_app *a)
{
    int node = vt_cur_node(a);
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], def[3 * VT_SPEC_MAX], label[96];
    if (node < 0) return;
    if (a->t.n[node].parent < 0) { vt_set_msg(a, 1, "A disk can't be grafted"); return; }
    if (a->t.n[node].flags & VT_N_CLOSED) {
        vt_set_msg(a, 1, "%s is an alias or unchecked entry: VTREE won't move it", a->t.n[node].name);
        return;
    }
    vt_tree_spec(&a->t, a->t.n[node].parent, dev, sizeof dev, dir, sizeof dir);
    snprintf(def, sizeof def, "%s%s", dev, dir);
    snprintf(label, sizeof label, "Graft %.30s to: ", a->t.n[node].name);
    vt_start_prompt(a, P_GRAFT_TO, label, def);
}

void vt_op_graft_done(vt_app *a, const char *text)
{
    int node = vt_cur_node(a), parent = a->t.n[node].parent, dnode;
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], sdev[VT_SPEC_MAX], pdir[VT_SPEC_MAX];
    char from[3 * VT_SPEC_MAX], to[3 * VT_SPEC_MAX], res[VT_SPEC_MAX], q[3 * VT_SPEC_MAX];
    unsigned short old = 0;
    int changed;
    unsigned int st;
    vt_file *e;
    st = vtp_locate(text, dev, sizeof dev, dir, sizeof dir);
    if (!(st & 1)) { vt_status_msg(a, text, st); return; }
    vt_tree_spec(&a->t, parent, sdev, sizeof sdev, pdir, sizeof pdir);
    if (vt_strcasecmp(sdev, dev)) { vt_set_msg(a, 1, "A graft stays on one disk"); return; }
    if (!vt_strcasecmp(pdir, dir)) { vt_set_msg(a, 0, "It is there already"); return; }
    dnode = vt_find_node(a, dev, dir);
    if (dnode >= 0 && vt_tree_within(&a->t, node, dnode)) {
        vt_set_msg(a, 1, "Refused: %s is inside the branch being moved", dir);
        return;
    }
    {   /* the same test by name, for a destination not in the tree */
        char me[2 * VT_SPEC_MAX], mine[VT_SPEC_MAX];
        vt_tree_spec(&a->t, node, sdev, sizeof sdev, mine, sizeof mine);
        snprintf(me, sizeof me, "%.*s", (int) strlen(mine) - 1, mine);
        if (!vt_strncasecmp(dir, me, strlen(me)) && (dir[strlen(me)] == ']' || dir[strlen(me)] == '.')) {
            vt_set_msg(a, 1, "Refused: %s is inside the branch being moved", dir);
            return;
        }
    }
    if (dnode >= 0) {
        vt_load_files(a, dnode);
        if (vt_tree_find_child(&a->t, dnode, a->t.n[node].name) >= 0) {
            vt_set_msg(a, 1, "%s%s has a %s already", dev, dir, a->t.n[node].name);
            return;
        }
    }
    e = vt_dir_entry(a, node);
    if (!e) { vt_set_msg(a, 1, "%s.DIR;1 is not in its parent's listing", a->t.n[node].name); return; }
    if ((e->a.prot & 0x0080) && e->a.uic != a->sess.uic) {
        vt_set_msg(a, 1, "%s.DIR has no delete access, which moving it needs, and isn't yours", a->t.n[node].name);
        return;
    }
    snprintf(q, sizeof q, "Move the branch %s from %s to %s?%s  Y yes  N no", a->t.n[node].name, pdir, dir,
             (e->a.prot & 0x0080) ? " (allowing delete access just for the move)" : "");
    if (vt_ask(a, q, "YN", 'N') != 'Y') { vt_set_msg(a, 0, "Nothing done"); return; }
    snprintf(from, sizeof from, "%s%s%s.DIR;1", dev, pdir, a->t.n[node].name);
    snprintf(to, sizeof to, "%s%s%s.DIR;1", dev, dir, a->t.n[node].name);
    if (!allow_delete(a, e, from, &old, &changed)) { vt_set_msg(a, 1, "Can't allow delete access on %s", from); return; }
    st = vtp_rename(from, to, res, sizeof res);
    vt_log_op(a, "GRAFT", from, (st & 1) ? res : to, st);
    if (changed) vt_log_op(a, "PROT", (st & 1) ? res : from, 0, vtp_set_prot((st & 1) ? res : from, old));
    if (!(st & 1)) { vt_status_msg(a, a->t.n[node].name, st); return; }
    if (dnode >= 0) vt_tree_move(&a->t, node, dnode);
    else vt_tree_remove(&a->t, node);
    vt_refresh(a, parent);
    vt_sync_children(a, parent);
    if (dnode >= 0) {
        vt_file *ne;
        vt_refresh(a, dnode);
        /* the moved directory must now be a plain entry of its new parent: if its
           back-link did not follow, it would show as an alias (N5) */
        ne = vt_dir_entry(a, node);
        if (ne && (ne->flags & VT_F_ALIAS)) {
            a->t.n[node].flags |= VT_N_ALIAS;
            vt_set_msg(a, 1, "Moved, but its back-link still points to the old parent: it shows as an alias");
            vt_rebuild_vis(a);
            return;
        }
    }
    vt_rebuild_vis(a);
    if (dnode >= 0) vt_set_cursor_node(a, node);
    vt_set_msg(a, 0, "Moved %s to %s%s", a->t.n[node].name, dev, dir);
}
