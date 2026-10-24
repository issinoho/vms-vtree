/* test_core.c - unit checks of src/core (pure code). Builds on Linux and VMS. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vt_util.h"
#include "vt_match.h"
#include "vt_edit.h"
#include "vt_tree.h"
#include "vt_screen.h"

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
        CHECK(t.n[al].flags & VT_N_ALIAS);
        CHECK(t.n[below].flags & VT_N_ALIAS);    /* everything under an alias is one */
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

int main(void)
{
    test_util();
    test_match();
    test_edit();
    test_tree();
    test_screen();
    printf("test_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
