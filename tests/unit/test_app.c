/* test_app.c - the whole app against the stub platform: keys in, screens out.
   Each case's screen is compared with tests/snap/<name>.txt; run with
   VT_UPDATE_SNAPSHOTS=1 to write them (then read every changed one). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vt_app.h"
#include "vt_stub.h"
#include "vt_util.h"

static int checks, fails;
static const char *snapdir = "tests/snap";
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void world(void)
{
    stub_reset();
    stub_device("DKA0:", "VMSSYS", 1234567, 4194304);
    stub_device("DKA100:", "USERDISK", 900000, 2000000);
    stub_dir("DKA0:", "[USERS.IAIN.VMS_VTREE]");
    stub_dir("DKA0:", "[USERS.IAIN.COM]");
    stub_dir("DKA0:", "[USERS.GUEST]");
    stub_dir("DKA0:", "[SYS0.SYSEXE]");
    stub_dir("DKA0:", "[VMS$COMMON.SYSEXE]");
    stub_dir("DKA0:", "[VMS$COMMON.SYSLIB]");
    stub_alias("DKA0:", "[SYS0]", "SYSCOMMON", "[VMS$COMMON]");
    stub_dir_denied("DKA0:", "[USERS.SYSTEM]");
    stub_dir("DKA0:", "[USERS.ODD.INNER]");
    stub_nolink("DKA0:", "[USERS]", "ODD");
    stub_file("DKA0:", "[USERS.ODD.INNER]", "BEHIND.TXT;1", 1, 0, 0);
    stub_default("DKA0:", "[USERS.IAIN]");
    stub_file("DKA0:", "[USERS.IAIN]", "LOGIN.COM;12", 2, vt_make_time(2026, 10, 23, 8, 15, 0),
              "$ set noon\n$ define/nolog vtree$test yes\n$ ! a comment with a\ttab\n$ exit\n");
    stub_file("DKA0:", "[USERS.IAIN]", "LOGIN.COM;11", 2, 0, 0);
    stub_file("DKA0:", "[USERS.IAIN]", "NOTES.TXT;1", 40, vt_make_time(2026, 9, 2, 17, 0, 0), 0);
    stub_file("DKA0:", "[USERS.IAIN]", "SECRET.DAT;1", 7, 0, 0);
    stub_file("DKA0:", "[USERS.IAIN]", "MAKEFILE.;1", 3, 0, 0);
    stub_file("DKA0:", "[USERS.IAIN.VMS_VTREE]", "README.MD;7", 5, 0, 0);
    stub_file("DKA0:", "[USERS.IAIN.VMS_VTREE]", "BUILD_VMS.COM;3", 4, 0, 0);
    stub_file("DKA0:", "[USERS.IAIN.COM]", "BACKUP.COM;2", 1, 0, 0);
    stub_file("DKA0:", "[VMS$COMMON.SYSEXE]", "DCL.EXE;1", 900, 0, 0);
    stub_file("DKA0:", "[VMS$COMMON.SYSLIB]", "STARLET.OLB;1", 9000, 0, 0);
}

static void snap(vt_app *a, const char *name)
{
    static char text[65536], want[65536];
    char path[256];
    FILE *f;
    size_t n;
    vt_app_draw(a);
    vt_screen_text(&a->scr, text, sizeof text);
    snprintf(path, sizeof path, "%s/%s.txt", snapdir, name);
    checks++;
    if (getenv("VT_UPDATE_SNAPSHOTS")) {
        f = fopen(path, "w");
        if (f) { fputs(text, f); fclose(f); }
        return;
    }
    f = fopen(path, "r");
    if (!f) { fails++; printf("FAIL snapshot %s missing\n", path); return; }
    n = fread(want, 1, sizeof want - 1, f);
    want[n] = '\0';
    fclose(f);
    if (strcmp(text, want)) {
        fails++;
        printf("FAIL snapshot %s differs; got:\n%s\n", name, text);
    }
}

static int nrefs(vt_app *a) { if (a->refs_dirty) vt_rebuild_refs(a); return a->nrefs; }

static void keys(vt_app *a, const int *k)
{
    for (; *k != VK_NONE; k++) {
        vt_app_key(a, *k);
        while (vt_app_idle(a)) ;
        vt_app_draw(a);               /* as the main loop does before each key */
    }
}

static void start(vt_app *a)
{
    world();
    vtp_start(&a->sess);
    CHECK(vt_app_init(a, 0, 24, 80));
    vtp_start(&a->sess);
    while (vt_app_idle(a)) ;
}

int main(int argc, char **argv)
{
    vt_app a;
    if (argc > 1) snapdir = argv[1];

    /* 1. Start in the default directory: the path is open, its files shown. */
    start(&a);
    snap(&a, "01_start");
    CHECK(vt_strcasecmp(a.t.n[vt_cur_node(&a)].name, "IAIN") == 0);

    /* 2. The file window, one line per file, then the Info page. */
    { int k[] = { VK_RETURN, '=', VK_DOWN, VK_NONE }; keys(&a, k); }
    snap(&a, "02_files_long");
    { int k[] = { 'I', VK_NONE }; keys(&a, k); }
    snap(&a, "03_info");
    { int k[] = { ' ', VK_NONE }; keys(&a, k); }
    CHECK(a.mode == M_FILES);

    /* 3. Tagging and the filespec; versions. */
    { int k[] = { '=', VK_UP, 'T', 'T', 'F', VK_CTRL_U, '*', '.', 'C', 'O', 'M', VK_RETURN, VK_NONE }; keys(&a, k); }
    snap(&a, "04_filespec_tagged");
    { int k[] = { ';', VK_NONE }; keys(&a, k); }
    CHECK(nrefs(&a) == 1);
    { int k[] = { ';', 'F', VK_CTRL_U, VK_RETURN, VK_NONE }; keys(&a, k); }
    CHECK(nrefs(&a) == 5);

    /* 4. The viewer, Find and hex. */
    { int k[] = { VK_PF1, VK_UP, 'V', VK_NONE }; keys(&a, k); }
    CHECK(a.mode == M_VIEW);
    snap(&a, "05_view");
    { int k[] = { VK_FIND, 'e', 'x', 'i', 't', VK_RETURN, VK_NONE }; keys(&a, k); }
    CHECK(a.view.top == 3);
    { int k[] = { 'H', VK_NONE }; keys(&a, k); }
    snap(&a, "06_hex");
    { int k[] = { VK_F10, VK_F10, VK_NONE }; keys(&a, k); }
    CHECK(a.mode == M_TREE);

    /* 5. The tree: open everything below the root; the alias is not entered,
          the protected directory is marked, and Showall leaves the alias out. */
    { int k[] = { VK_PF1, VK_UP, '*', VK_NONE }; keys(&a, k); }
    snap(&a, "07_tree_all");
    { int k[] = { 'S', VK_NONE }; keys(&a, k); }
    snap(&a, "08_showall");
    {
        int i, saw_dcl = 0;
        for (i = 0; i < nrefs(&a); i++)
            if (!strcmp(vt_ref_file(&a, &a.refs[i])->name, "DCL")) saw_dcl++;
        CHECK(saw_dcl == 1);           /* once, from VMS$COMMON, not again via SYSCOMMON */
    }
    {
        int i, behind = 0;
        for (i = 0; i < nrefs(&a); i++)
            if (!strcmp(vt_ref_file(&a, &a.refs[i])->name, "BEHIND")) behind++;
        CHECK(behind == 0);            /* not through the unchecked ODD */
    }
    { int k[] = { VK_F10, VK_NONE }; keys(&a, k); }

    /* 6. Opening the alias by hand works, and is marked. */
    {
        int i, node = -1;
        for (i = 0; i < a.t.count; i++) if (!strcmp(a.t.n[i].name, "SYSCOMMON")) node = i;
        CHECK(node >= 0 && (a.t.n[node].flags & VT_N_ALIAS));
        CHECK(!(a.t.n[node].flags & VT_N_SCANNED));
    }

    /* 6b. A branch started at the unchecked directory itself does go in. */
    {
        int i, odd = -1, behind = 0;
        for (i = 0; i < a.t.count; i++) if (!strcmp(a.t.n[i].name, "ODD")) odd = i;
        CHECK(odd >= 0 && (a.t.n[odd].flags & VT_N_NOLINK));
        { int k[] = { VK_PF1, VK_UP, VK_NONE }; keys(&a, k); }
        while (vt_cur_node(&a) != odd && a.tcur < a.nvis - 1) { int k[] = { VK_DOWN, VK_NONE }; keys(&a, k); }
        CHECK(vt_cur_node(&a) == odd);
        { int k[] = { 'B', VK_NONE }; keys(&a, k); }
        for (i = 0; i < nrefs(&a); i++)
            if (!strcmp(vt_ref_file(&a, &a.refs[i])->name, "BEHIND")) behind++;
        CHECK(behind == 1);
        { int k[] = { VK_F10, VK_NONE }; keys(&a, k); }
    }

    /* 7. Help, Avail and logging another disk. */
    { int k[] = { VK_HELP, VK_NONE }; keys(&a, k); }
    snap(&a, "09_help");
    { int k[] = { ' ', 'A', VK_NONE }; keys(&a, k); }
    snap(&a, "10_avail");
    { int k[] = { VK_F10, 'L', VK_CTRL_U, 'D', 'K', 'A', '0', ':', '[', 'N', 'O', 'P', 'E', ']', VK_RETURN, VK_NONE }; keys(&a, k); }
    CHECK(a.msg_err && strstr(a.msg, "DNF"));
    { int k[] = { 'Q', 'N', VK_NONE }; keys(&a, k); }
    CHECK(!a.quit);
    { int k[] = { 'Q', 'Y', VK_NONE }; keys(&a, k); }
    CHECK(a.quit);
    vt_app_free(&a);

    /* 9. Phase 2: copy, move, rename, make directory, edit, subprocess. */
    world();
    stub_limit("DKA0:", "[USERS.IAIN.COM]", 2);
    stub_file("DKA0:", "[USERS.IAIN.COM]", "LOGIN.COM;1", 1, 0, 0);
    CHECK(vt_app_init(&a, 0, 24, 80));
    vtp_start(&a.sess);
    while (vt_app_idle(&a)) continue;
    {   /* copy LOGIN.COM;12 as *.BAK into VMS_VTREE, pointing at it in the tree */
        int k[] = { VK_RETURN, 'C', VK_CTRL_U, '*', '.', 'B', 'A', 'K', VK_RETURN, VK_SELECT, VK_NONE };
        keys(&a, k);
        CHECK(a.picking);
        snap(&a, "12_pick");
    }
    {
        int k[] = { VK_DOWN, VK_DOWN, VK_RETURN, VK_NONE };   /* COM, then VMS_VTREE */
        keys(&a, k);
        CHECK(!a.picking && a.prompt == P_COPY_TO);
        CHECK(strstr(a.edit.buf, "[USERS.IAIN.VMS_VTREE]") != 0);
    }
    { int k[] = { VK_RETURN, VK_NONE }; keys(&a, k); }
    CHECK(strstr(stub_log(), "COPY   DKA0:[USERS.IAIN]LOGIN.COM;12 -> DKA0:[USERS.IAIN.VMS_VTREE]LOGIN.BAK;1") != 0);
    CHECK(strstr(a.msg, "Copy: 1 done") != 0);
    snap(&a, "13_copied");

    /* both LOGIN.COM versions, tagged, copied to a new directory: oldest first, so
       ;11 becomes ;1 and ;12 becomes ;2; the new directory is asked about first */
    {
        int k[] = { VK_PF1, VK_UP, 'T', 'T', VK_PF1, 'C', VK_RETURN, VK_CTRL_U, 'D', 'K', 'A', '0', ':', '[',
                    'U', 'S', 'E', 'R', 'S', '.', 'I', 'A', 'I', 'N', '.', 'N', 'E', 'W', ']', VK_NONE };
        int ans[] = { 'Y', 'Y', VK_NONE };              /* create it; copy 2 files */
        keys(&a, k);
        stub_keys(ans);
        { int k2[] = { VK_RETURN, VK_NONE }; keys(&a, k2); }
        CHECK(strstr(stub_log(), "LOGIN.COM;11 -> DKA0:[USERS.IAIN.NEW]LOGIN.COM;1") != 0);
        CHECK(strstr(stub_log(), "LOGIN.COM;12 -> DKA0:[USERS.IAIN.NEW]LOGIN.COM;2") != 0);
        CHECK(strstr(stub_log(), "MKDIR") != 0);
        CHECK(vt_find_node(&a, "DKA0:", "[USERS.IAIN.NEW]") >= 0);
    }
    {   /* copying them into COM (limit 2, one there already) is refused, nothing done */
        int k[] = { VK_PF1, VK_UP, 'T', 'T', VK_PF1, 'C', VK_RETURN, VK_CTRL_U, 'D', 'K', 'A', '0', ':', '[',
                    'U', 'S', 'E', 'R', 'S', '.', 'I', 'A', 'I', 'N', '.', 'C', 'O', 'M', ']', VK_RETURN, VK_NONE };
        size_t before = strlen(stub_log());
        keys(&a, k);
        CHECK(strstr(a.msg, "Refused") && strstr(a.msg, "RMS would delete"));
        CHECK(strlen(stub_log()) == before);
    }
    {   /* a failing copy in the middle: S skips it and the rest goes on */
        int k[] = { VK_PF1, 'T', VK_PF1, 'C', VK_RETURN, VK_CTRL_U, 'D', 'K', 'A', '0', ':', '[',
                    'U', 'S', 'E', 'R', 'S', '.', 'G', 'U', 'E', 'S', 'T', ']', VK_NONE };
        int ans[] = { 'Y', 'S', VK_NONE };
        stub_fail_copy("NOTES");
        keys(&a, k);
        stub_keys(ans);
        { int k2[] = { VK_RETURN, VK_NONE }; keys(&a, k2); }
        CHECK(strstr(stub_log(), "NOTES.TXT;1 -> DKA0:[USERS.GUEST]NOTES.TXT  %SYSTEM-F-NOPRIV") != 0);
        CHECK(strstr(stub_log(), "SECRET.DAT;1 -> DKA0:[USERS.GUEST]SECRET.DAT;1") != 0);
        CHECK(strstr(a.msg, "Copy: 4 done") != 0);
    }
    {   /* move to another disk: refused */
        int k[] = { VK_PF1, VK_UP, 'M', VK_CTRL_U, 'D', 'K', 'A', '1', '0', '0', ':', '[', '0', '0', '0', '0', '0', '0', ']', VK_RETURN, VK_NONE };
        stub_dir("DKA100:", "[X]");
        keys(&a, k);
        CHECK(strstr(a.msg, "another disk") != 0);
    }
    {   /* rename the two tagged LOGIN.COMs to *.OLD: each keeps its version */
        int k[] = { VK_PF1, 'U', VK_PF1, VK_UP, 'T', 'T', VK_PF1, 'R', VK_CTRL_U, '*', '.', 'O', 'L', 'D', VK_NONE };
        int ans[] = { 'Y', VK_NONE };
        keys(&a, k);
        stub_keys(ans);
        { int k2[] = { VK_RETURN, VK_NONE }; keys(&a, k2); }
        CHECK(strstr(stub_log(), "RENAME DKA0:[USERS.IAIN]LOGIN.COM;11 -> DKA0:[USERS.IAIN]LOGIN.OLD;11") != 0);
        CHECK(strstr(stub_log(), "RENAME DKA0:[USERS.IAIN]LOGIN.COM;12 -> DKA0:[USERS.IAIN]LOGIN.OLD;12") != 0);
        CHECK(strstr(a.msg, "Rename: 2 done") != 0);
    }
    {   /* one file can't take a fixed name for two */
        int k[] = { VK_PF1, VK_UP, 'T', 'T', VK_PF1, 'R', VK_CTRL_U, 'X', '.', 'Y', VK_RETURN, VK_NONE };
        keys(&a, k);
        CHECK(strstr(a.msg, "can't all get the name") != 0);
    }
    vt_app_free(&a);

    /* 10. Make, rename a directory; edit; subprocess; a move within the disk. */
    world();
    CHECK(vt_app_init(&a, 0, 24, 80));
    vtp_start(&a.sess);
    while (vt_app_idle(&a)) continue;
    { int k[] = { 'M', 'T', 'O', 'O', 'L', 'S', VK_RETURN, VK_NONE }; keys(&a, k); }
    CHECK(!strcmp(a.t.n[vt_cur_node(&a)].name, "TOOLS"));
    CHECK(strstr(stub_log(), "MKDIR  DKA0:[USERS.IAIN.TOOLS]") != 0);
    { int k[] = { 'M', 'A', '.', 'B', VK_RETURN, VK_NONE }; keys(&a, k); }
    CHECK(strstr(a.msg, "Not a directory name") != 0);
    {   /* no delete access on the new directory: asked, allowed, put back */
        int ans[] = { 'Y', VK_NONE };
        int k[] = { 'R', VK_CTRL_U, 'B', 'I', 'N', VK_RETURN, VK_NONE };
        stub_keys(ans);
        keys(&a, k);
    }
    CHECK(!strcmp(a.t.n[vt_cur_node(&a)].name, "BIN"));
    CHECK(strstr(stub_log(), "PROT   DKA0:[USERS.IAIN]BIN.DIR;1") != 0);
    {
        int ans[] = { 'N', VK_NONE };
        int k[] = { 'R', VK_CTRL_U, 'B', 'I', 'N', '2', VK_RETURN, VK_NONE };
        stub_keys(ans);
        keys(&a, k);
        CHECK(!strcmp(a.t.n[vt_cur_node(&a)].name, "BIN"));      /* refused: unchanged */
        CHECK(strstr(a.msg, "Nothing done") != 0);
    }
    CHECK(strstr(stub_log(), "[USERS.IAIN]TOOLS.DIR;1 -> DKA0:[USERS.IAIN]BIN.DIR;1") != 0);
    snap(&a, "14_renamed_dir");
    {
        int i, sys = -1;
        for (i = 0; i < a.t.count; i++) if (!strcmp(a.t.n[i].name, "SYSCOMMON")) sys = i;
        (void) sys;
    }
    { int k[] = { VK_LEFT, VK_RETURN, 'E', VK_NONE }; keys(&a, k); }
    CHECK(!strcmp(stub_spawned(), "DKA0:[USERS.IAIN]LOGIN.COM;12"));
    { int k[] = { 'X', VK_NONE }; keys(&a, k); }
    CHECK(!strcmp(stub_spawned(), "DCL"));
    {   /* move NOTES.TXT into BIN: same version, gone from IAIN */
        int k[] = { VK_PF1, VK_DOWN, VK_LEFT, 'M', VK_CTRL_U, 'D', 'K', 'A', '0', ':', '[', 'U', 'S', 'E', 'R',
                    'S', '.', 'I', 'A', 'I', 'N', '.', 'B', 'I', 'N', ']', VK_RETURN, VK_NONE };
        keys(&a, k);
        CHECK(strstr(stub_log(), "MOVE   DKA0:[USERS.IAIN]NOTES.TXT;1 -> DKA0:[USERS.IAIN.BIN]NOTES.TXT;1") != 0);
        CHECK(nrefs(&a) == 4);
    }
    vt_app_free(&a);

    /* 8. A 132-column, 30-row screen. */
    world();
    CHECK(vt_app_init(&a, "DKA0:[USERS.IAIN.VMS_VTREE]", 30, 132));
    vtp_start(&a.sess);
    while (vt_app_idle(&a)) continue;
    snap(&a, "11_wide");
    vt_app_free(&a);

    printf("test_app: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
