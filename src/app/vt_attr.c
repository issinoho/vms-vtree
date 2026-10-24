/* vt_attr.c - Phase 4: protection, owner, ACL, expiration date and two file flags
   (the attributes screen), and searching, comparing and printing files.
   Every change shows old -> new and asks (default No, D10), and is logged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vt_app.h"
#include "vt_util.h"
#include "vt_plan.h"

/* ---------- the attributes screen ---------- */

static int attr_target(vt_app *a, vt_src *s, vt_attrs *at, vtp_security *sec)
{
    /* fresh: the file's directory read again, the file found again by its ID */
    vt_flist *fl;
    int i;
    vt_load_files(a, s->node);
    fl = &a->t.n[s->node].files;
    for (i = 0; i < fl->n; i++)
        if (!memcmp(fl->f[i].fid, s->fid, 6)) {
            *at = fl->f[i].a;
            vtp_security_get(s->path, sec);
            return 1;
        }
    return 0;
}

static void attr_build(vt_app *a)
{
    vt_attrs at;
    vtp_security sec;
    char b[VT_SPEC_MAX + 64], v[64];
    int i;
    vt_text_free(a);
    a->attr_nacl = 0;
    if (a->op_n == 1) {
        if (!attr_target(a, &a->op_src[0], &at, &sec)) {
            vt_text_add(a, "The file is no longer there.");
            return;
        }
        snprintf(b, sizeof b, "%-18s %s", "File", a->op_src[0].path); vt_text_add(a, b);
        vt_text_add(a, "");
        snprintf(b, sizeof b, "%-18s %s", "P  Protection", vt_fmt_prot(at.prot, v, sizeof v)); vt_text_add(a, b);
        snprintf(b, sizeof b, "%-18s %s  %s", "O  Owner", sec.owner, vt_fmt_uic(at.uic, v, sizeof v)); vt_text_add(a, b);
        snprintf(b, sizeof b, "%-18s %s", "E  Expires", at.edt ? vt_fmt_time(at.edt, 0, v, sizeof v) : "<None specified>");
        vt_text_add(a, b);
        snprintf(b, sizeof b, "%-18s %s", "B  Backup", (at.uchar & VTP_CH_NOBACKUP) ? "No (/NOBACKUP)" : "Yes");
        vt_text_add(a, b);
        snprintf(b, sizeof b, "%-18s %s", "Z  Erase on delete", (at.uchar & VTP_CH_ERASE) ? "Yes" : "No");
        vt_text_add(a, b);
        vt_text_add(a, "");
        if (!(sec.acl_status & 1)) {
            vtp_message(sec.acl_status, v, sizeof v);
            snprintf(b, sizeof b, "%-18s %s", "Access Cntrl List", v); vt_text_add(a, b);
        } else if (!sec.acl_count) vt_text_add(a, "Access Cntrl List  None   (A adds an entry)");
        else {
            vt_text_add(a, "Access Cntrl List  (Up/Down choose an entry, D deletes it, A adds one)");
            a->attr_acl0 = a->ninfo;
            for (i = 0; i < sec.acl_count; i++) {
                snprintf(b, sizeof b, "   %s", sec.acl[i]);
                vt_text_add(a, b);
                vt_strlcpy(a->attr_acl[i], sec.acl[i], sizeof a->attr_acl[i]);
            }
            a->attr_nacl = sec.acl_count;
        }
        a->attr_cur_prot = at.prot;
        a->attr_cur_uchar = at.uchar;
        a->attr_cur_edt = at.edt;
    } else {
        snprintf(b, sizeof b, "%d tagged files:", a->op_n); vt_text_add(a, b);
        for (i = 0; i < a->op_n && i < a->rows - 12; i++) { snprintf(b, sizeof b, "   %s", a->op_src[i].path); vt_text_add(a, b); }
        if (a->op_n > a->rows - 12) { snprintf(b, sizeof b, "   ... and %d more", a->op_n - (a->rows - 12)); vt_text_add(a, b); }
        vt_text_add(a, "");
        vt_text_add(a, "P protection   O owner   A add an ACL entry   E expiration date");
        vt_text_add(a, "B backup on/off   Z erase on delete on/off   -- each is asked, then done to all");
        a->attr_cur_prot = 0; a->attr_cur_uchar = 0; a->attr_cur_edt = 0;
    }
    if (a->attr_sel >= a->attr_nacl) a->attr_sel = a->attr_nacl ? a->attr_nacl - 1 : 0;
}

void vt_attr_open(vt_app *a, int tagged)
{
    int n;
    if (a->mode == M_TREE) {
        /* a directory: its .DIR file in the parent */
        int node = vt_cur_node(a), parent;
        vt_file *e;
        if (node < 0 || (parent = a->t.n[node].parent) < 0) { vt_set_msg(a, 1, "A disk has no attributes here"); return; }
        if (a->t.n[node].flags & VT_N_CLOSED) { vt_set_msg(a, 1, "%s is an alias or unchecked entry: refused", a->t.n[node].name); return; }
        e = vt_dir_entry(a, node);
        if (!e) { vt_set_msg(a, 1, "%s.DIR;1 not found", a->t.n[node].name); return; }
        free(a->op_src);
        a->op_src = (vt_src *) calloc(1, sizeof (vt_src));
        if (!a->op_src) return;
        a->op_n = 1;
        a->op_src[0].node = parent;
        memcpy(a->op_src[0].fid, e->fid, 6);
        {
            char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX];
            vt_tree_spec(&a->t, parent, dev, sizeof dev, dir, sizeof dir);
            char full[3 * VT_SPEC_MAX];
            snprintf(full, sizeof full, "%s%s%s.DIR;1", dev, dir, e->name);
            vt_strlcpy(a->op_src[0].path, full, sizeof a->op_src[0].path);
        }
    } else {
        n = vt_collect(a, tagged);
        if (!n) { vt_set_msg(a, 1, tagged ? "No files are tagged in this list" : "No file here"); return; }
    }
    a->attr_back = a->mode;
    a->attr_sel = 0;
    a->mode = M_ATTR;
    attr_build(a);
}

/* Apply one change to every target; log it; report. kind: P O A D X E B Z */
static void apply(vt_app *a, int kind, unsigned short prot, unsigned int uic, const char *ace,
                  vt_time t, unsigned int set, unsigned int clear, const char *what)
{
    int i, done = 0, failed = 0;
    for (i = 0; i < a->op_n; i++) {
        const char *p = a->op_src[i].path;
        unsigned int st;
        char line[VT_SPEC_MAX + 200], v[64];
        switch (kind) {
        case 'P': st = vtp_set_prot(p, prot); snprintf(line, sizeof line, "%s %s", p, vt_fmt_prot(prot, v, sizeof v)); vt_log_op(a, "PROT", line, 0, st); break;
        case 'O': st = vtp_set_owner(p, uic); snprintf(line, sizeof line, "%s %s", p, vt_fmt_uic(uic, v, sizeof v)); vt_log_op(a, "OWNER", line, 0, st); break;
        case 'A': st = vtp_acl_add(p, ace); snprintf(line, sizeof line, "%s %s", p, ace); vt_log_op(a, "ACL+", line, 0, st); break;
        case 'D': st = vtp_acl_delete(p, ace); snprintf(line, sizeof line, "%s %s", p, ace); vt_log_op(a, "ACL-", line, 0, st); break;
        case 'X': st = vtp_acl_delete_all(p); snprintf(line, sizeof line, "%s (all entries)", p); vt_log_op(a, "ACL-", line, 0, st); break;
        case 'E': st = vtp_set_expiry(p, t); snprintf(line, sizeof line, "%s %s", p, t ? vt_fmt_time(t, 0, v, sizeof v) : "none"); vt_log_op(a, "EXPIRE", line, 0, st); break;
        default:  st = vtp_set_flags(p, set, clear); snprintf(line, sizeof line, "%s %s", p, what); vt_log_op(a, "FLAGS", line, 0, st); break;
        }
        if (st & 1) { done++; continue; }
        failed++;
        if (i + 1 < a->op_n) {
            char m[160], q[VT_SPEC_MAX + 240];
            vtp_message(st, m, sizeof m);
            snprintf(q, sizeof q, "%.60s: %.80s   S skip it  Q stop", p, m);
            if (vt_ask(a, q, "SQ", 'Q') != 'S') break;
        } else {
            char m[160];
            vtp_message(st, m, sizeof m);
            vt_set_msg(a, 1, "%s: %s", p, m);
        }
    }
    if (!failed) vt_set_msg(a, 0, "%s: done for %d file%s", what, done, done == 1 ? "" : "s");
    else if (done) vt_set_msg(a, 1, "%s: %d done, %d failed (see the log)", what, done, failed);
    for (i = 0; i < a->op_n; i++) vt_refresh(a, a->op_src[i].node);
    attr_build(a);
}

static int confirm(vt_app *a, const char *what)
{
    char q[VT_SPEC_MAX + 200];
    if (a->op_n == 1) snprintf(q, sizeof q, "%s?  Y yes  N no", what);
    else snprintf(q, sizeof q, "%s, for all %d tagged files?  Y yes  N no", what, a->op_n);
    if (vt_ask(a, q, "YN", 'N') == 'Y') return 1;
    vt_set_msg(a, 0, "Nothing changed");
    return 0;
}

void vt_attr_prompt_done(vt_app *a, int kind, const char *text)
{
    char what[VT_SPEC_MAX + 128], o[64], n[64];
    switch (kind) {
    case P_PROT: {
        unsigned short np;
        if (!vt_parse_prot(text, a->attr_cur_prot, &np)) {
            vt_set_msg(a, 1, "Not a protection: %s  (e.g. S:RWED,O:RWED,G:RE,W)", text);
            return;
        }
        if (a->op_n == 1) snprintf(what, sizeof what, "Protection %s -> %s", vt_fmt_prot(a->attr_cur_prot, o, sizeof o), vt_fmt_prot(np, n, sizeof n));
        else snprintf(what, sizeof what, "Protection -> %s", vt_fmt_prot(np, n, sizeof n));
        if (a->op_n > 1) {
            /* each file keeps its own value for the classes not named */
            int i, done = 0;
            if (!confirm(a, what)) return;
            for (i = 0; i < a->op_n; i++) {
                vt_attrs at;
                vtp_security sec;
                unsigned short each;
                unsigned int st;
                char line[VT_SPEC_MAX + 64];
                if (!attr_target(a, &a->op_src[i], &at, &sec) || !vt_parse_prot(text, at.prot, &each)) continue;
                st = vtp_set_prot(a->op_src[i].path, each);
                snprintf(line, sizeof line, "%s %s -> %s", a->op_src[i].path, vt_fmt_prot(at.prot, o, sizeof o), vt_fmt_prot(each, n, sizeof n));
                vt_log_op(a, "PROT", line, 0, st);
                if (st & 1) done++;
            }
            vt_set_msg(a, done != a->op_n, "Protection: %d of %d done", done, a->op_n);
            attr_build(a);
            return;
        }
        if (confirm(a, what)) apply(a, 'P', np, 0, 0, 0, 0, 0, "Protection");
        return;
    }
    case P_OWNER: {
        unsigned int uic;
        if (!vt_parse_uic(text, &uic) && !(vtp_name_uic(text, &uic) & 1)) {
            vt_set_msg(a, 1, "Not a user or UIC: %s  (e.g. IAIN or [200,101])", text);
            return;
        }
        snprintf(what, sizeof what, "Owner -> %s", vt_fmt_uic(uic, n, sizeof n));
        if (confirm(a, what)) apply(a, 'O', 0, uic, 0, 0, 0, 0, "Owner");
        return;
    }
    case P_ACE:
        snprintf(what, sizeof what, "Add the ACL entry %s", text);
        if (confirm(a, what)) apply(a, 'A', 0, 0, text, 0, 0, 0, "ACL entry added");
        return;
    case P_EXPIRY: {
        vt_time t;
        if (!vt_parse_date(text, &t)) { vt_set_msg(a, 1, "Not a date: %s  (e.g. 1-JAN-2030, or NONE)", text); return; }
        snprintf(what, sizeof what, "Expiration date -> %s", t ? vt_fmt_time(t, 0, n, sizeof n) : "none");
        if (confirm(a, what)) apply(a, 'E', 0, 0, 0, t, 0, 0, "Expiration date");
        return;
    }
    default: return;
    }
}

static void flag(vt_app *a, unsigned int bit, const char *on_text, const char *off_text)
{
    int on;
    char what[96];
    if (a->op_n == 1) on = !(a->attr_cur_uchar & bit);
    else {
        int k = vt_ask(a, bit == VTP_CH_NOBACKUP ? "For all tagged files:  N no backup  B back up (normal)  F10 neither"
                                                  : "For all tagged files:  E erase on delete  N don't  F10 neither",
                       bit == VTP_CH_NOBACKUP ? "NB" : "EN", 0);
        if (!k) return;
        on = bit == VTP_CH_NOBACKUP ? k == 'N' : k == 'E';
    }
    snprintf(what, sizeof what, "%s", on ? on_text : off_text);
    if (confirm(a, what)) apply(a, 'F', 0, 0, 0, 0, on ? bit : 0, on ? 0 : bit, what);
}

void vt_attr_key(vt_app *a, int key)
{
    int gold = a->gold;
    char def[VT_SPEC_MAX];
    a->gold = 0;
    switch (key) {
    case VK_PF1: a->gold = !gold; return;
    case VK_UP: if (a->attr_sel > 0) a->attr_sel--; return;
    case VK_DOWN: if (a->attr_sel + 1 < a->attr_nacl) a->attr_sel++; return;
    case 'P': case 'p':
        vt_fmt_prot(a->attr_cur_prot, def, sizeof def);
        vt_start_prompt(a, P_PROT, a->op_n == 1 ? "Protection: " : "Protection (classes not named stay): ",
                        a->op_n == 1 ? def : "");
        return;
    case 'O': case 'o': vt_start_prompt(a, P_OWNER, "Owner (user name or [g,m]): ", ""); return;
    case 'A': case 'a': vt_start_prompt(a, P_ACE, "Add ACL entry: ", "(IDENTIFIER=,ACCESS=READ)"); return;
    case 'E': case 'e':
        if (a->attr_cur_edt) vt_fmt_time(a->attr_cur_edt, 0, def, sizeof def); else vt_strlcpy(def, "NONE", sizeof def);
        vt_start_prompt(a, P_EXPIRY, "Expiration date (or NONE): ", a->op_n == 1 ? def : "NONE");
        return;
    case 'B': case 'b': flag(a, VTP_CH_NOBACKUP, "Mark it /NOBACKUP", "Let backups take it again"); return;
    case 'Z': case 'z': flag(a, VTP_CH_ERASE, "Erase on delete: on", "Erase on delete: off"); return;
    case 'D': case 'd': case VK_REMOVE:
        if (a->op_n != 1) { vt_set_msg(a, 1, "Deleting ACL entries works on one file at a time"); return; }
        if (gold) {
            if (!a->attr_nacl) { vt_set_msg(a, 0, "There is no ACL"); return; }
            if (confirm(a, "Delete the whole ACL")) apply(a, 'X', 0, 0, 0, 0, 0, 0, "ACL deleted");
            return;
        }
        if (!a->attr_nacl) { vt_set_msg(a, 0, "There is no ACL entry to delete"); return; }
        {
            char what[200], ace[128];
            vt_strlcpy(ace, a->attr_acl[a->attr_sel], sizeof ace);
            snprintf(what, sizeof what, "Delete the ACL entry %s", ace);
            if (confirm(a, what)) apply(a, 'D', 0, 0, ace, 0, 0, 0, "ACL entry deleted");
        }
        return;
    case VK_F10: case VK_CTRL_Z: case VK_RETURN: case 'Q': case 'q':
        vt_text_free(a);
        a->mode = a->attr_back;
        return;
    default: return;
    }
}

void vt_attr_draw(vt_app *a)
{
    vt_screen *s = &a->scr;
    int i;
    vt_draw_title(a, a->op_n == 1 ? "Attributes and security" : "Attributes and security: tagged files");
    vt_box(s, 1, 0, s->rows - 3, s->cols - 1);
    for (i = 0; i < a->ninfo && 2 + i < s->rows - 3; i++) {
        unsigned char at = 0;
        if (a->attr_nacl && i == a->attr_acl0 + a->attr_sel) at = VT_A_REV;
        vt_put(s, 2 + i, 2, a->info[i], at, s->cols - 4);
    }
    if (a->gold)
        vt_draw_bottom(a, "GOLD", "&Delete the whole ACL", "PF1 cancel GOLD");
    else
        vt_draw_bottom(a, "ATTR", "&Protection &Owner &Add-ACE &Delete-ACE &Expires &Backup &Zero-on-delete",
                       "Up/Down choose an ACE   PF1 GOLD   F10 back");
}

/* ---------- search the contents of files ---------- */

void vt_op_search(vt_app *a)
{
    int n = 0;
    {   /* the tagged files, or the current one */
        int i, tagged = 0;
        if (a->refs_dirty) vt_rebuild_refs(a);
        for (i = 0; i < a->nrefs; i++) if (vt_ref_file(a, &a->refs[i])->flags & VT_F_TAGGED) tagged = 1;
        n = vt_collect(a, tagged);
    }
    if (!n) { vt_set_msg(a, 1, "No file here"); return; }
    vt_start_prompt(a, P_SEARCH, n == 1 ? "Search this file for: " : "Search the tagged files for: ", a->view.find);
}

void vt_op_search_done(vt_app *a, const char *text)
{
    static char rec[4096];
    char line[VT_SPEC_MAX + 128], title[128];
    int i, hits = 0, stopped = 0, files_hit = 0, unreadable = 0;
    if (!text[0]) return;
    vt_strlcpy(a->view.find, text, sizeof a->view.find);
    vt_text_free(a);
    for (i = 0; i < a->op_n && !stopped && hits < 2000; i++) {
        vtp_view *v = 0;
        vtp_pos pos;
        int len, lineno = 0, here = 0;
        unsigned int st = vtp_view_open(a->op_src[i].path, 0, &v);
        if (!(st & 1)) { unreadable++; continue; }
        while ((st = vtp_view_next(v, rec, sizeof rec - 1, &len, &pos)) & 1) {
            lineno++;
            if (len > (int) sizeof rec - 1) len = (int) sizeof rec - 1;
            rec[len] = '\0';
            if (vt_strcasestr(rec, text)) {
                if (!here) { vt_text_add(a, a->op_src[i].path); here = 1; files_hit++; }
                {   /* tabs as spaces (to the next multiple of 8 within the text) */
                    char t[200];
                    int c, o = 0;
                    for (c = 0; rec[c] && o < (int) sizeof t - 9 && o < 120; c++) {
                        if (rec[c] == '\t') do t[o++] = ' '; while (o % 8 && o < (int) sizeof t - 1);
                        else t[o++] = rec[c];
                    }
                    t[o] = '\0';
                    snprintf(line, sizeof line, "  %6d  %s", lineno, t);
                }
                vt_text_add(a, line);
                if (++hits >= 2000) break;
            }
            if (lineno % 5000 == 0) {
                snprintf(line, sizeof line, "Searching %s: line %d   (any key stops)", a->op_src[i].name, lineno);
                vt_progress(a, line);
                if (vt_check_stop(a)) { a->pushback = VK_NONE; stopped = 1; break; }
            }
        }
        vtp_view_close(v);
    }
    if (!hits) {
        vt_text_free(a);
        vt_set_msg(a, unreadable != 0, "\"%s\" not found in %d file%s%s", text, a->op_n, a->op_n == 1 ? "" : "s",
                   unreadable ? " (some could not be read)" : "");
        return;
    }
    snprintf(title, sizeof title, "Search: \"%.40s\" - %d line%s in %d file%s%s%s", text, hits, hits == 1 ? "" : "s",
             files_hit, files_hit == 1 ? "" : "s", stopped ? " (stopped)" : "", hits >= 2000 ? " (first 2000)" : "");
    vt_strlcpy(a->text_title, title, sizeof a->text_title);
    vt_strlcpy(a->text_label, "SEARCH", sizeof a->text_label);
    a->back_mode = a->mode;
    a->mode = M_INFO;
    a->itop = 0;
    vt_set_msg(a, 0, "");
}

/* ---------- compare two files (DIFFERENCES), print ---------- */

void vt_op_compare(vt_app *a)
{
    int n, i, tagged = 0, count, highest, limit;
    char dev[VT_SPEC_MAX], dir[VT_SPEC_MAX], out[3 * VT_SPEC_MAX], cmd[3 * VT_SPEC_MAX + 64], path[3 * VT_SPEC_MAX + 16];
    char name[40];
    unsigned int st;
    if (a->refs_dirty) vt_rebuild_refs(a);
    for (i = 0; i < a->nrefs; i++) if (vt_ref_file(a, &a->refs[i])->flags & VT_F_TAGGED) tagged++;
    if (tagged != 2) { vt_set_msg(a, 1, "Tag exactly two files to compare (%d tagged)", tagged); return; }
    n = vt_collect(a, 1);
    if (n != 2) return;
    /* two versions of one file: the older first, so the listing shows what changed */
    if (!vt_strcasecmp(a->op_src[0].name, a->op_src[1].name) && !vt_strcasecmp(a->op_src[0].type, a->op_src[1].type)
        && a->op_src[0].ver > a->op_src[1].ver) {
        vt_src t = a->op_src[0];
        a->op_src[0] = a->op_src[1];
        a->op_src[1] = t;
    }
    st = vtp_locate("SYS$SCRATCH:", dev, sizeof dev, dir, sizeof dir);
    if (!(st & 1)) { vt_status_msg(a, "SYS$SCRATCH", st); return; }
    snprintf(name, sizeof name, "VTREE_DIFF_%u", (unsigned int) (vtp_now() % 100000u));
    snprintf(out, sizeof out, "%s%s%s.LIS", dev, dir, name);
    snprintf(cmd, sizeof cmd, "DIFFERENCES/OUTPUT=%s $A $B", out);
    vt_progress(a, "Comparing...");
    st = vtp_run(cmd, a->op_src[0].path, a->op_src[1].path);
    if (!(vtp_versions(dev, dir, name, ".LIS", &count, &highest, &limit) & 1) || !count) {
        vt_status_msg(a, "DIFFERENCES", st);
        return;
    }
    snprintf(path, sizeof path, "%s;%d", out, highest);
    vt_view_open_path(a, path, 1);        /* deleted again when the viewer closes */
}

void vt_op_print(vt_app *a, int tagged)
{
    int n = vt_collect(a, tagged), i, done = 0;
    char q[200];
    if (!n) { vt_set_msg(a, 1, tagged ? "No files are tagged in this list" : "No file here"); return; }
    snprintf(q, sizeof q, "Print %d file%s on SYS$PRINT?  Y yes  N no", n, n == 1 ? "" : "s");
    if (vt_ask(a, q, "YN", 'N') != 'Y') { vt_set_msg(a, 0, "Nothing printed"); return; }
    for (i = 0; i < n; i++) {
        unsigned int st = vtp_run("PRINT $A", a->op_src[i].path, 0);
        vt_log_op(a, "PRINT", a->op_src[i].path, 0, st);
        if (st & 1) done++;
    }
    vt_set_msg(a, done != n, "Printing: %d of %d queued on SYS$PRINT", done, n);
}
