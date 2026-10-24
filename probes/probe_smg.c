/* probe_smg.c - Phase 0 probe 1: SMG$ screen and keys on a (pseudo-)terminal.

   Draws a bordered display with a DEC line-drawing tree, reverse/bold text and a
   two-line menu, then reads up to MAX_KEYS keystrokes and logs each terminator code
   and its SMG$ name to PROBE_SMG.LOG. Ends on "q", after MAX_KEYS keys, or on a
   10-second read timeout, so it can never hang (NOTES N2 in vms-histsh).

   Run interactively, or under PTYDRIVE with probe_smg_keys.txt. */
#include <stdio.h>
#include <string.h>
#include <descrip.h>
#include <smg$routines.h>
#include <smgdef.h>
#include <smgmsg.h>
#include <ssdef.h>
#include "probe_common.h"

#define MAX_KEYS 40

static FILE *logf;

static void put(unsigned int disp, int row, int col, const char *s, unsigned int rend)
{
    struct dsc$descriptor_s d;
    desc_set(&d, s, (unsigned short) strlen(s));
    smg$put_chars(&disp, &d, &row, &col, 0, &rend, 0, 0);
}

static void put_gfx(unsigned int disp, int row, int col, const char *s)
{
    /* SMG$C_SPEC_GRAPHICS: the DEC Special Graphics set (q = -, x = |, t = |-, m = L). */
    struct dsc$descriptor_s d;
    unsigned int rend = 0, comp = 0, cs = SMG$C_SPEC_GRAPHICS;
    desc_set(&d, s, (unsigned short) strlen(s));
    smg$put_chars(&disp, &d, &row, &col, 0, &rend, &comp, &cs);
}

int main(void)
{
    unsigned int pb = 0, kb = 0, main_d = 0, menu_d = 0, st;
    int rows = 0, cols = 0, type = 0, drows, dcols, prow = 2, pcol = 1, one = 1;
    unsigned int border = SMG$M_BORDER, none = 0;
    unsigned short term;
    int timeout = 10, n;
    char namebuf[64];
    unsigned short namelen;
    struct dsc$descriptor_s name, lab;

    logf = fopen("PROBE_SMG.LOG", "w");
    if (!logf) return SS$_ABORT;

    st = smg$create_pasteboard(&pb, 0, &rows, &cols, 0, &type, 0);
    fprintf(logf, "create_pasteboard %s rows=%d cols=%d type=%d\n", msg(st), rows, cols, type);
    if (!(st & 1)) { fclose(logf); return st; }
    st = smg$create_virtual_keyboard(&kb, 0, 0, 0, 0);
    fprintf(logf, "create_virtual_keyboard %s\n", msg(st));

    drows = rows - 5; dcols = cols - 2;
    smg$create_virtual_display(&drows, &dcols, &main_d, &border, 0, 0);
    desc_set(&lab, " DKA0:[USERS.IAIN] ", 19);
    smg$label_border(&main_d, &lab, 0, 0, 0, 0, 0);
    put(main_d, 1, 2, "DKA0:", SMG$M_BOLD);
    put_gfx(main_d, 2, 2, "tqq");  put(main_d, 2, 5, "USERS", 0);
    put_gfx(main_d, 3, 2, "x mqq"); put(main_d, 3, 7, "IAIN", SMG$M_REVERSE);
    put_gfx(main_d, 4, 2, "mqq");  put(main_d, 4, 5, "SYS0", 0);
    put(main_d, 6, 2, "Normal  ", 0);
    put(main_d, 6, 10, "Bold  ", SMG$M_BOLD);
    put(main_d, 6, 16, "Reverse  ", SMG$M_REVERSE);
    put(main_d, 6, 25, "Underline", SMG$M_UNDERLINE);
    smg$paste_virtual_display(&main_d, &pb, &prow, &pcol, 0);

    drows = 2; dcols = cols;
    smg$create_virtual_display(&drows, &dcols, &menu_d, &none, 0, 0);
    put(menu_d, 1, 1, "DIR       Copy Delete Filespec Log Makedir Rename Tag Untag View", 0);
    put(menu_d, 2, 1, "COMMANDS  Gold Return file window  Help  q quit", SMG$M_REVERSE);
    prow = rows - 1;
    smg$paste_virtual_display(&menu_d, &pb, &prow, &one, 0);

    for (n = 0; n < MAX_KEYS; n++) {
        st = smg$read_keystroke(&kb, &term, 0, &timeout, 0, 0, 0);
        if (st == SS$_TIMEOUT) { fprintf(logf, "key timeout\n"); break; }
        if (!(st & 1)) { fprintf(logf, "read_keystroke %s\n", msg(st)); break; }
        desc_set(&name, namebuf, sizeof namebuf - 1);
        namelen = 0;
        if (smg$keycode_to_name(&term, &name) & 1) {
            /* The name comes back blank-padded in a fixed-length string. */
            namebuf[sizeof namebuf - 1] = '\0';
            namebuf[63] = '\0';
            for (namelen = 0; namelen < 63 && namebuf[namelen] != ' '; namelen++) ;
            namebuf[namelen] = '\0';
        } else strcpy(namebuf, "?");
        fprintf(logf, "key %3u %s\n", term, namebuf);
        put(main_d, 8, 2, "Last key:                    ", 0);
        put(main_d, 8, 12, namebuf, SMG$M_BOLD);
        if (term == 'q') break;
    }

    st = smg$delete_pasteboard(&pb, &one);
    fprintf(logf, "delete_pasteboard %s\n", msg(st));
    fclose(logf);
    return SS$_NORMAL;
}
