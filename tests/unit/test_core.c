/* test_core.c - unit checks of src/core (pure code). Builds on Linux and VMS. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vt_util.h"
#include "vt_match.h"
#include "vt_edit.h"
#include "vt_tree.h"
#include "vt_screen.h"
#include "vt_ansi.h"
#include "vt_plan.h"
#include "vt_platform.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b))) { fails++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static void test_util(void)
{
    char b[128];
    CHECK_STR(vt_commas(0, b, sizeof b), "0");
    CHECK_STR(vt_commas(999, b, sizeof b), "999");
    CHECK_STR(vt_commas(1000, b, sizeof b), "1,000");
    CHECK_STR(vt_commas(1234567, b, sizeof b), "1,234,567");
    CHECK_STR(vt_fmt_prot(0xFA00, b, sizeof b), "(S:RWED,O:RWED,G:RE,W)");
    CHECK_STR(vt_fmt_prot(0xEE88, b, sizeof b), "(S:RWE,O:RWE,G:R,W:R)");
    CHECK_STR(vt_fmt_prot(0x0000, b, sizeof b), "(S:RWED,O:RWED,G:RWED,W:RWED)");
    CHECK_STR(vt_fmt_uic((0200u << 16) | 0101u, b, sizeof b), "[200,101]");
    /* 17-NOV-1858 is zero; a known VMS time: 1-JAN-1970 = 0x007C95674BEB4000 */
    CHECK(vt_make_time(1970, 1, 1, 0, 0, 0) == 0x007C95674BEB4000ULL);
    CHECK(vt_make_time(1858, 11, 17, 0, 0, 0) == 0);
    CHECK_STR(vt_fmt_time(vt_make_time(2026, 10, 24, 15, 2, 7), 1, b, sizeof b), "24-OCT-2026 15:02:07.00");
    CHECK_STR(vt_fmt_time(vt_make_time(2000, 2, 29, 0, 0, 0), 0, b, sizeof b), "29-FEB-2000 00:00");
    CHECK_STR(vt_fmt_time(vt_make_time(2026, 1, 5, 9, 5, 0), 0, b, sizeof b), " 5-JAN-2026 09:05");
    CHECK_STR(vt_fmt_time(0, 0, b, sizeof b), "");

    CHECK(vt_dir_child("[000000]", "USERS", b, sizeof b) && !strcmp(b, "[USERS]"));
    CHECK(vt_dir_child("[USERS]", "IAIN", b, sizeof b) && !strcmp(b, "[USERS.IAIN]"));
    strcpy(b, "[USERS.][IAIN]"); vt_dir_normalise(b); CHECK_STR(b, "[USERS.IAIN]");
    strcpy(b, "[USERS.][iain.SUB]"); vt_dir_normalise(b); CHECK_STR(b, "[USERS.iain.SUB]");
    strcpy(b, "<A.B>"); vt_dir_normalise(b); CHECK_STR(b, "[A.B]");
    strcpy(b, "[000000.A]"); vt_dir_normalise(b); CHECK_STR(b, "[A]");
    strcpy(b, "[A^.B.C]"); vt_dir_normalise(b); CHECK_STR(b, "[A^.B.C]");
    {
        char comp[8][VT_NAME_MAX];
        CHECK(vt_dir_split("[000000]", comp, 8) == 0);
        CHECK(vt_dir_split("[USERS.IAIN]", comp, 8) == 2);
        CHECK_STR(comp[0], "USERS"); CHECK_STR(comp[1], "IAIN");
        CHECK(vt_dir_split("[A^.B.C]", comp, 8) == 2);
        CHECK_STR(comp[0], "A^.B"); CHECK_STR(comp[1], "C");
    }
    CHECK(vt_strcasecmp("Login", "LOGIN") == 0);
    CHECK(vt_strcasestr("Hello World", "WORLD") != 0);
    CHECK(vt_strcasestr("Hello", "xyz") == 0);
    CHECK(vt_strlcpy(b, "abcdef", 4) == 6 && !strcmp(b, "abc"));
}

static void test_match(void)
{
    vt_filter f;
    CHECK(vt_wild("*", ""));
    CHECK(vt_wild("*.COM", "LOGIN.COM"));
    CHECK(vt_wild("log%n", "LOGIN"));
    CHECK(!vt_wild("LOG%", "LOGIN"));
    CHECK(vt_wild("*A*B*", "xxAyyBzz"));
    CHECK(!vt_wild("*A*B", "xxAyyBzz"));

    CHECK(vt_filter_parse(&f, ""));
    CHECK_STR(f.text, "*.*;*");
    CHECK(vt_filter_parse(&f, "*.COM"));
    CHECK_STR(f.text, "*.COM;*");
    CHECK(vt_filter_match(&f, "LOGIN", ".COM", 3, 0));
    CHECK(!vt_filter_match(&f, "LOGIN", ".TXT", 3, 1));
    CHECK(vt_filter_parse(&f, "login"));
    CHECK_STR(f.text, "login.*;*");
    CHECK(vt_filter_parse(&f, "*.C;*, *.H"));
    CHECK(f.count == 2);
    CHECK(vt_filter_match(&f, "X", ".H", 1, 0));
    CHECK(vt_filter_parse(&f, "*.*;0"));
    CHECK(vt_filter_match(&f, "X", ".H", 5, 1));
    CHECK(!vt_filter_match(&f, "X", ".H", 4, 0));
    CHECK(vt_filter_parse(&f, "X.H;4"));
    CHECK(vt_filter_match(&f, "X", ".H", 4, 0));
    CHECK(!vt_filter_match(&f, "X", ".H", 5, 1));
    CHECK(vt_filter_parse(&f, "*."));               /* files with no type */
    CHECK(vt_filter_match(&f, "MAKEFILE", ".", 1, 1));
    CHECK(!vt_filter_match(&f, "MAKEFILE", ".C", 1, 1));
    CHECK(vt_filter_parse(&f, "A^.B.TXT"));         /* ODS-5 escaped dot in a name */
    CHECK_STR(f.p[0].name, "A^.B");
    CHECK(!vt_filter_parse(&f, "DKA0:[X]*.*"));
    CHECK(!vt_filter_parse(&f, "*.C,,*.H"));
    CHECK(!vt_filter_parse(&f, "X.C;abc"));
}

static void type(vt_edit *e, const char *s) { while (*s) vt_edit_key(e, (unsigned char) *s++); }

static void test_edit(void)
{
    vt_edit e;
    vt_edit_init(&e, "*.COM");
    CHECK(e.pos == 5);
    vt_edit_key(&e, VK_DEL); vt_edit_key(&e, VK_DEL); vt_edit_key(&e, VK_DEL);
    type(&e, "C");
    CHECK_STR(e.buf, "*.C");
    vt_edit_key(&e, VK_CTRL_H); type(&e, "X");
    CHECK_STR(e.buf, "X*.C");
    vt_edit_key(&e, VK_CTRL_E); vt_edit_key(&e, VK_LEFT); vt_edit_key(&e, VK_CTRL_U);
    CHECK_STR(e.buf, "C");
    vt_edit_key(&e, VK_CTRL_A); vt_edit_key(&e, VK_CTRL_H); type(&e, "H");
    CHECK_STR(e.buf, "H");
    CHECK(vt_edit_key(&e, VK_RETURN) == VT_EDIT_DONE);
    CHECK(vt_edit_key(&e, VK_F10) == VT_EDIT_CANCEL);
}

static void test_tree(void)
{
    vt_tree t;
    unsigned short fid[3] = { 1, 2, 3 };
    int root, users, iain, guest, sys, vis[16], n;
    char dev[64], dir[256];
    vt_flist fl;

    vt_tree_init(&t);
    root = vt_tree_root(&t, "DKA0:");
    CHECK(vt_tree_root(&t, "dka0:") == root);
    users = vt_tree_child(&t, root, "USERS", fid, 0);
    sys = vt_tree_child(&t, root, "SYS0", fid, 0);
    iain = vt_tree_child(&t, users, "IAIN", fid, 0);
    guest = vt_tree_child(&t, users, "GUEST", fid, 0);
    CHECK(vt_tree_child(&t, users, "iain", fid, 0) == iain);
    CHECK(t.n[root].child == sys);               /* name order: SYS0 < USERS */
    CHECK(t.n[users].child == guest);
    vt_tree_spec(&t, iain, dev, sizeof dev, dir, sizeof dir);
    CHECK_STR(dev, "DKA0:"); CHECK_STR(dir, "[USERS.IAIN]");
    vt_tree_spec(&t, root, dev, sizeof dev, dir, sizeof dir);
    CHECK_STR(dir, "[000000]");
    n = vt_tree_visible(&t, vis, 16);
    CHECK(n == 1);
    t.n[root].flags |= VT_N_OPEN;
    t.n[users].flags |= VT_N_OPEN;
    n = vt_tree_visible(&t, vis, 16);
    CHECK(n == 5 && vis[0] == root && vis[1] == sys && vis[2] == users && vis[3] == guest && vis[4] == iain);
    CHECK(vt_tree_is_last(&t, iain) && !vt_tree_is_last(&t, guest));
    CHECK(vt_tree_within(&t, users, iain) && !vt_tree_within(&t, sys, iain));
    {
        int al = vt_tree_child(&t, sys, "SYSCOMMON", fid, 1);
        int below = vt_tree_child(&t, al, "SYSEXE", fid, 0);
        int unk = vt_tree_child(&t, users, "ODD", fid, 2);
        int under = vt_tree_child(&t, unk, "X", fid, 0);
        CHECK(t.n[al].flags & VT_N_ALIAS);
        CHECK(!vt_tree_reaches(&t, root, below));     /* through the alias: no */
        CHECK(vt_tree_reaches(&t, al, below));        /* started at the alias: yes */
        CHECK(!vt_tree_reaches(&t, users, under));    /* through an unchecked entry: no */
        CHECK(vt_tree_reaches(&t, users, iain));
        CHECK(vt_tree_reach_top(&t, below) == al);
        CHECK(vt_tree_reach_top(&t, iain) == root);
    }
    vt_tree_free(&t);

    memset(&fl, 0, sizeof fl);
    vt_flist_add(&fl, "A", ".TXT", 3, fid, 0, 0);
    vt_flist_add(&fl, "A", ".TXT", 2, fid, 0, 0);
    vt_flist_add(&fl, "B", ".TXT", 1, fid, 0, 0);
    vt_flist_mark_highest(&fl);
    CHECK((fl.f[0].flags & VT_F_HIGHEST) && !(fl.f[1].flags & VT_F_HIGHEST) && (fl.f[2].flags & VT_F_HIGHEST));
    vt_flist_clear(&fl);
    free(fl.f);
}

static void test_screen(void)
{
    vt_screen s;
    char out[2048];
    vt_screen_init(&s, 3, 10);
    vt_box(&s, 0, 0, 2, 9);
    vt_put(&s, 1, 1, "Hi\tthere, long text", 0, 8);
    vt_screen_text(&s, out, sizeof out);
    CHECK_STR(out, "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n"
                   "\xe2\x94\x82Hi?there\xe2\x94\x82\n"
                   "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\n");
    CHECK(vt_at(&s, 3, 0) == 0 && vt_at(&s, 0, 10) == 0);
    vt_screen_free(&s);
}

static char got[4096]; static int gotn;
static void grab(void *ctx, const char *b, int n)
{
    (void) ctx;
    if (gotn + n < (int) sizeof got) { memcpy(got + gotn, b, (size_t) n); gotn += n; got[gotn] = '\0'; }
}

static void test_ansi(void)
{
    vt_screen s;
    vt_cell prev[2 * 4];
    vt_screen_init(&s, 2, 4);
    vt_put(&s, 0, 0, "ab", 0, -1);
    vt_put(&s, 0, 2, "C", VT_A_BOLD, -1);
    vt_gfx(&s, 1, 0, VT_G_HLINE, 0);
    vt_put(&s, 1, 1, "X", VT_A_REV, -1);
    /* the palette */
    CHECK_STR(vt_ansi_sgr(vt_at(&s, 0, 0), 1), "0;37;44");     /* white on blue */
    CHECK_STR(vt_ansi_sgr(vt_at(&s, 0, 2), 1), "0;1;33;44");   /* yellow key */
    CHECK_STR(vt_ansi_sgr(vt_at(&s, 1, 0), 1), "0;36;44");     /* cyan frame */
    CHECK_STR(vt_ansi_sgr(vt_at(&s, 1, 1), 1), "0;36;40;7");   /* black on cyan bar, as reverse video (D13) */
    CHECK_STR(vt_ansi_sgr(vt_at(&s, 1, 1), 0), "0;7");         /* mono: reverse */
    vt_role(&s, VT_R_MENU_LABEL);
    vt_put(&s, 0, 3, "D", VT_A_REV, -1);
    CHECK_STR(vt_ansi_sgr(vt_at(&s, 0, 3), 1), "0;1;36;44");   /* DIR / COMMANDS */
    vt_role(&s, VT_R_AUTO);
    /* a full frame, then only what changed */
    gotn = 0;
    vt_ansi_render(&s, 0, 1, grab, 0);
    CHECK(strstr(got, "\033[1;1H\033[0;37;44mab\033[0;1;33;44mC") != 0);
    CHECK(strstr(got, "\033(0q\033[0;36;40;7m\033(BX") != 0);
    memcpy(prev, s.cell, sizeof prev);
    vt_put(&s, 0, 1, "z", 0, -1);
    gotn = 0;
    vt_ansi_render(&s, prev, 1, grab, 0);
    CHECK_STR(got, "\033[?25l\033[1;2H\033[0;37;44mz");
    gotn = 0;
    vt_ansi_render(&s, prev, 1, grab, 0);
    CHECK_STR(got, "\033[?25l");                               /* nothing changed */
    vt_screen_free(&s);
}

static void test_plan(void)
{
    /* VTREE's own conditions must read as failures */
    CHECK(!(VTP_STOPPED & 1) && !(VTP_NOTEXACT & 1) && !(VTP_DIFFERENT & 1));
    char n[64], t[64];
    CHECK(vt_name_apply("*.BAK", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK_STR(n, "LOGIN"); CHECK_STR(t, ".BAK");
    CHECK(vt_name_apply("NEW.*", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK_STR(n, "NEW"); CHECK_STR(t, ".COM");
    CHECK(vt_name_apply("OTHER", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK_STR(n, "OTHER"); CHECK_STR(t, ".COM");
    CHECK(vt_name_apply("*.*", "MAKEFILE", ".", n, sizeof n, t, sizeof t));
    CHECK_STR(n, "MAKEFILE"); CHECK_STR(t, ".");
    CHECK(vt_name_apply("X.", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK_STR(t, ".");
    CHECK(vt_name_apply("A^.B.TXT", "Z", ".Q", n, sizeof n, t, sizeof t));
    CHECK_STR(n, "A^.B"); CHECK_STR(t, ".TXT");
    CHECK(!vt_name_apply("A*.COM", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK(!vt_name_apply("X.COM;2", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK(!vt_name_apply("[X]Y.Z", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK(!vt_name_apply(".COM", "LOGIN", ".COM", n, sizeof n, t, sizeof t));
    CHECK(vt_pattern_wild("*.BAK") && !vt_pattern_wild("X.BAK"));
    CHECK(vt_valid_dirname("NEW_DIR$1-A"));
    CHECK(!vt_valid_dirname("") && !vt_valid_dirname("A.B") && !vt_valid_dirname("A B"));
    CHECK(!vt_valid_dirname("[X]") && !vt_valid_dirname("1234567890123456789012345678901234567890"));
    {
        unsigned short pr;
        unsigned int u;
        vt_time t;
        char b[64];
        CHECK(vt_parse_prot("(S:RWED,O:RWED,G:RE,W)", 0, &pr) && pr == 0xFA00);
        CHECK(vt_parse_prot("G:R,W", 0xFA00, &pr) && !strcmp(vt_fmt_prot(pr, b, sizeof b), "(S:RWED,O:RWED,G:R,W)"));
        CHECK(vt_parse_prot("world:re", 0xFA00, &pr) && !strcmp(vt_fmt_prot(pr, b, sizeof b), "(S:RWED,O:RWED,G:RE,W:RE)"));
        CHECK(vt_parse_prot("SYSTEM=RWED, OWNER=DREW", 0xFFFF, &pr) && (pr & 0xFF) == 0);
        CHECK(vt_parse_prot("o:", 0x0000, &pr) && pr == 0x00F0);
        CHECK(!vt_parse_prot("X:R", 0, &pr) && !vt_parse_prot("", 0, &pr));
        CHECK(!vt_parse_prot("S:RWQ", 0, &pr) && !vt_parse_prot("(S:R", 0, &pr));
        CHECK(!vt_parse_prot("SYSTEMS:R", 0, &pr));
        CHECK(vt_parse_uic("[200,101]", &u) && u == ((0200u << 16) | 0101u));
        CHECK(!vt_parse_uic("[IAIN]", &u) && !vt_parse_uic("[8,1]", &u) && !vt_parse_uic("[1,]", &u));
        CHECK(vt_parse_date("1-JAN-2030", &t) && t == vt_make_time(2030, 1, 1, 0, 0, 0));
        CHECK(vt_parse_date("29-feb-2028 14:30", &t) && t == vt_make_time(2028, 2, 29, 14, 30, 0));
        CHECK(vt_parse_date(" 5-Oct-2026 01:02:03 ", &t) && t == vt_make_time(2026, 10, 5, 1, 2, 3));
        CHECK(vt_parse_date("none", &t) && t == 0);
        CHECK(!vt_parse_date("29-FEB-2027", &t) && !vt_parse_date("32-JAN-2030", &t));
        CHECK(!vt_parse_date("1-XYZ-2030", &t) && !vt_parse_date("1-JAN-30", &t) && !vt_parse_date("1-JAN-2030 25:00", &t));
    }
    CHECK(vt_version_room(0, 0, 1, 0) == VT_ROOM_OK);
    CHECK(vt_version_room(2, 2, 1, 3) == VT_ROOM_OK);
    CHECK(vt_version_room(3, 3, 1, 3) == VT_ROOM_LIMIT);
    CHECK(vt_version_room(3, 3, 1, 32767) == VT_ROOM_OK);
    CHECK(vt_version_room(1, 32767, 1, 0) == VT_ROOM_32767);
    {
        vt_nver f[6] = { { "A", ".TXT", 3 }, { "A", ".TXT", 1 }, { "a", ".txt", 2 },
                         { "B", ".TXT", 7 }, { "A", ".COM", 1 }, { "B", ".TXT", 5 } };
        int out[6], n;
        n = vt_purge_select(f, 6, 1, out);
        CHECK(n == 3 && !out[0] && out[1] && out[2] && !out[3] && !out[4] && out[5]);
        n = vt_purge_select(f, 6, 2, out);
        CHECK(n == 1 && out[1] && !out[2] && !out[5]);
        n = vt_purge_select(f, 6, 0, out);              /* 0 means 1: never all */
        CHECK(n == 3);
    }
    {
        vt_tree tr;
        unsigned short fid[3] = { 1, 1, 0 };
        int r, a, b, c, d, vis[16];
        vt_tree_init(&tr);
        r = vt_tree_root(&tr, "D:");
        a = vt_tree_child(&tr, r, "A", fid, 0);
        b = vt_tree_child(&tr, a, "B", fid, 0);
        c = vt_tree_child(&tr, r, "C", fid, 0);
        d = vt_tree_child(&tr, b, "D", fid, 0);
        CHECK(!vt_tree_move(&tr, a, d));                /* not into its own branch */
        CHECK(!vt_tree_move(&tr, a, a));
        CHECK(vt_tree_move(&tr, b, c));                 /* graft B (with D) under C */
        CHECK(tr.n[b].parent == c && tr.n[c].child == b && tr.n[a].child == -1);
        CHECK(tr.n[b].depth == 2 && tr.n[d].depth == 3);
        vt_tree_remove(&tr, c);
        CHECK(tr.n[r].child == a && tr.n[a].next == -1);
        CHECK((tr.n[b].flags & VT_N_DEAD) && (tr.n[d].flags & VT_N_DEAD));
        tr.n[r].flags |= VT_N_OPEN; tr.n[a].flags |= VT_N_OPEN;
        CHECK(vt_tree_visible(&tr, vis, 16) == 2);
        vt_tree_free(&tr);
    }
    {
        vt_tree tr;
        unsigned short fid[3] = { 1, 1, 0 };
        int r, a, b, c;
        vt_tree_init(&tr);
        r = vt_tree_root(&tr, "D:");
        a = vt_tree_child(&tr, r, "AAA", fid, 0);
        b = vt_tree_child(&tr, r, "BBB", fid, 0);
        c = vt_tree_child(&tr, r, "CCC", fid, 0);
        CHECK(vt_tree_rename(&tr, a, "ZZZ"));
        CHECK(tr.n[r].child == b && tr.n[b].next == c && tr.n[c].next == a && tr.n[a].next == -1);
        CHECK(vt_tree_rename(&tr, a, "AB"));
        CHECK(tr.n[r].child == a && tr.n[a].next == b);
        vt_tree_free(&tr);
    }
}

int main(void)
{
    test_util();
    test_match();
    test_edit();
    test_tree();
    test_screen();
    test_ansi();
    test_plan();
    printf("test_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
