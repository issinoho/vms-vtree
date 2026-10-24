/* probe_alias.c - Phase 1 probe: directory aliases, physical names, privileges.

   Usage: PROBE_ALIAS "dirspec" ["parsespec"]
   1. For every *.DIR;1 in dirspec: its FID, the directory it was found in (DID),
      its header back-link and the directory bit. An alias entry (D8) should have a
      back-link that is not the DID it was found in.
   2. $PARSE parsespec with and without NAML$M_NOCONCEAL: device and directory.
   3. Turn SYSPRV off (temporary, prmflg 0) and exit: the caller then checks with
      SHOW PROCESS/PRIVILEGES that image rundown gave it back. Read-only. */
#include <stdio.h>
#include <string.h>
#include <rms.h>
#include <starlet.h>
#include <iodef.h>
#include <ssdef.h>
#include <fibdef.h>
#include <atrdef.h>
#include <prvdef.h>
#include "probe_common.h"

typedef struct { unsigned short size, type; void *addr; } atr_item;

static void dirs(const char *spec)
{
    struct FAB fab = cc$rms_fab;
    struct NAML naml = cc$rms_naml;
    char esa[NAML$C_MAXRSS + 1], rsa[NAML$C_MAXRSS + 1];
    unsigned short chan = 0;
    unsigned int st;

    fab.fab$l_fna = (char *) -1; fab.fab$b_fns = 0; fab.fab$l_naml = &naml;
    naml.naml$l_long_filename = (char *) spec;
    naml.naml$l_long_filename_size = (unsigned int) strlen(spec);
    naml.naml$l_long_expand = esa; naml.naml$l_long_expand_alloc = sizeof esa - 1;
    naml.naml$l_long_result = rsa; naml.naml$l_long_result_alloc = sizeof rsa - 1;
    st = sys$parse(&fab, 0, 0);
    if (!(st & 1)) { report("$PARSE", st); return; }
    while ((st = sys$search(&fab, 0, 0)) & 1) {
        struct fibdef fib;
        struct dsc$descriptor_s fibd, dev;
        unsigned short iosb[4], back[3] = { 0, 0, 0 };
        unsigned int uchar = 0, qs;
        atr_item atr[3];
        rsa[naml.naml$l_long_result_size] = '\0';
        if (!chan) {
            desc_set(&dev, (char *) &naml.naml$t_dvi[1], naml.naml$t_dvi[0]);
            if (!(sys$assign(&dev, &chan, 0, 0) & 1)) return;
        }
        memset(&fib, 0, sizeof fib);
        memcpy(&fib.fib$w_fid, naml.naml$w_fid, 6);
        desc_set(&fibd, (char *) &fib, sizeof fib);
        atr[0].size = 6; atr[0].type = ATR$C_BACKLINK; atr[0].addr = back;
        atr[1].size = 4; atr[1].type = ATR$C_UCHAR; atr[1].addr = &uchar;
        atr[2].size = 0; atr[2].type = 0; atr[2].addr = 0;
        qs = sys$qiow(0, chan, IO$_ACCESS, (void *) iosb, 0, 0, &fibd, 0, 0, 0, atr, 0);
        if (qs & 1) qs = iosb[0];
        printf("%-44.44s fid=(%u,%u,%u) did=(%u,%u,%u) back=(%u,%u,%u) dirbit=%u %s%s\n",
               rsa, naml.naml$w_fid[0], naml.naml$w_fid[1], naml.naml$w_fid[2],
               naml.naml$w_did[0], naml.naml$w_did[1], naml.naml$w_did[2],
               back[0], back[1], back[2], (uchar >> 13) & 1,
               (qs & 1) ? "" : msg(qs),
               (qs & 1) && memcmp(back, naml.naml$w_did, 6) ? "  <-- ALIAS" : "");
    }
    if (chan) sys$dassgn(chan);
}

static void parse(const char *spec, int noconceal)
{
    struct FAB fab = cc$rms_fab;
    struct NAML naml = cc$rms_naml;
    char esa[NAML$C_MAXRSS + 1];
    unsigned int st;
    fab.fab$l_fna = (char *) -1; fab.fab$b_fns = 0; fab.fab$l_naml = &naml;
    naml.naml$l_long_filename = (char *) spec;
    naml.naml$l_long_filename_size = (unsigned int) strlen(spec);
    naml.naml$l_long_expand = esa; naml.naml$l_long_expand_alloc = sizeof esa - 1;
    if (noconceal) naml.naml$b_nop |= NAML$M_NOCONCEAL;
    st = sys$parse(&fab, 0, 0);
    esa[(st & 1) ? naml.naml$l_long_expand_size : 0] = '\0';
    printf("parse %-12s %s: dev=\"%.*s\" dir=\"%.*s\" expanded=\"%s\"\n",
           noconceal ? "NOCONCEAL" : "default", (st & 1) ? "ok" : msg(st),
           (int) naml.naml$l_long_dev_size, naml.naml$l_long_dev,
           (int) naml.naml$l_long_dir_size, naml.naml$l_long_dir, esa);
}

int main(int argc, char **argv)
{
    union prvdef off;
    if (argc < 2) { printf("usage: PROBE_ALIAS dirspec [parsespec]\n"); return SS$_INSFARG; }
    dirs(argv[1]);
    if (argc > 2) { parse(argv[2], 0); parse(argv[2], 1); }
    memset(&off, 0, sizeof off);
    off.prv$v_sysprv = 1;
    printf("setprv SYSPRV off (temporary): %s\n",
           msg(sys$setprv(0, (struct _generic_64 *) &off, 0, 0)));
    return SS$_NORMAL;
}
