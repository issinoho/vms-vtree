/* probe_scan.c - Phase 0 probe 2: how fast can a directory tree be read?

   Usage: PROBE_SCAN "dev:[dir...]*.*;*"

   Pass 1: RMS $SEARCH (NAML, so ODS-5 long names work) over the whole spec,
           counting files and directories (*.DIR;1).
   Pass 2: the same search, plus each file's attributes read by an ACP QIO
           IO$_ACCESS on its file ID: record attributes (size), owner, protection,
           creation and revision dates. The file is never opened, so this takes no
           file locks and cannot be blocked by a writer.
   Prints counts, elapsed times, and the first few files' attributes. Read-only. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <rms.h>
#include <starlet.h>
#include <descrip.h>
#include <iodef.h>
#include <ssdef.h>
#include <fibdef.h>
#include <atrdef.h>
#include "probe_common.h"

typedef struct { unsigned short size, type; void *addr; } atr_item;

static unsigned int now_ms(void)
{
    unsigned int t[2];
    sys$gettim(t);
    /* 100 ns units; the low word wraps every ~7 minutes: fine for a probe. */
    return t[0] / 10000u + t[1] * 429497u;
}

struct tally { unsigned long files, dirs, acp_fail, blocks; };

static int scan(const char *spec, int with_attrs, struct tally *t)
{
    struct FAB fab = cc$rms_fab;
    struct NAML naml = cc$rms_naml;
    char esa[NAML$C_MAXRSS + 1], rsa[NAML$C_MAXRSS + 1];
    unsigned int st;
    unsigned short chan = 0;
    int shown = 0;

    fab.fab$l_fna = (char *) -1;
    fab.fab$b_fns = 0;
    fab.fab$l_naml = &naml;
    naml.naml$l_long_filename = (char *) spec;
    naml.naml$l_long_filename_size = (unsigned int) strlen(spec);
    naml.naml$l_long_expand = esa;
    naml.naml$l_long_expand_alloc = sizeof esa - 1;
    naml.naml$l_long_result = rsa;
    naml.naml$l_long_result_alloc = sizeof rsa - 1;

    st = sys$parse(&fab, 0, 0);
    if (!(st & 1)) { report("$PARSE", st); return 0; }

    memset(t, 0, sizeof *t);
    while ((st = sys$search(&fab, 0, 0)) & 1) {
        size_t tl = naml.naml$l_long_type_size;
        rsa[naml.naml$l_long_result_size] = '\0';
        if (tl == 4 && strncmp(naml.naml$l_long_type, ".DIR", 4) == 0) t->dirs++;
        else t->files++;

        if (with_attrs) {
            struct fibdef fib;
            struct dsc$descriptor_s fibd, dev;
            unsigned short iosb[4], uic_w[2], fpro = 0;
            unsigned int credate[2], revdate[2];
            unsigned char fat[ATR$S_RECATTR];
            atr_item atr[6];
            unsigned int qs, hiblk, efblk;

            if (!chan) {
                desc_set(&dev, (char *) &naml.naml$t_dvi[1], naml.naml$t_dvi[0]);
                qs = sys$assign(&dev, &chan, 0, 0);
                if (!(qs & 1)) { report("$ASSIGN", qs); return 0; }
            }
            memset(&fib, 0, sizeof fib);
            memcpy(&fib.fib$w_fid, naml.naml$w_fid, 6);
            desc_set(&fibd, (char *) &fib, sizeof fib);
            atr[0].size = ATR$S_RECATTR; atr[0].type = ATR$C_RECATTR; atr[0].addr = fat;
            atr[1].size = 4; atr[1].type = ATR$C_UIC;     atr[1].addr = uic_w;
            atr[2].size = 2; atr[2].type = ATR$C_FPRO;    atr[2].addr = &fpro;
            atr[3].size = 8; atr[3].type = ATR$C_CREDATE; atr[3].addr = credate;
            atr[4].size = 8; atr[4].type = ATR$C_REVDATE; atr[4].addr = revdate;
            atr[5].size = 0; atr[5].type = 0;             atr[5].addr = 0;
            qs = sys$qiow(0, chan, IO$_ACCESS, (void *) iosb, 0, 0,
                          &fibd, 0, 0, 0, atr, 0);
            if ((qs & 1)) qs = iosb[0];
            if (!(qs & 1)) {
                t->acp_fail++;
                if (t->acp_fail <= 3) printf("  ACP %s: %s\n", rsa, msg(qs));
                continue;
            }
            /* FAT block numbers are stored high word first (FAT$W_*BLKH/L). */
            hiblk = ((unsigned int) ((unsigned short *) (fat + 4))[0] << 16)
                  | ((unsigned short *) (fat + 4))[1];
            efblk = ((unsigned int) ((unsigned short *) (fat + 8))[0] << 16)
                  | ((unsigned short *) (fat + 8))[1];
            t->blocks += efblk;
            if (shown < 5) {
                printf("  %-50.50s rfm=%u org=%u eof=%u alloc=%u uic=[%o,%o] prot=%04X\n",
                       rsa, fat[0] & 0x0f, fat[0] >> 4, efblk, hiblk,
                       uic_w[1], uic_w[0], fpro);
                shown++;
            }
        }
    }
    if (chan) sys$dassgn(chan);
    if (st != RMS$_NMF && st != RMS$_FNF) report("$SEARCH ended", st);
    return 1;
}

int main(int argc, char **argv)
{
    struct tally t;
    unsigned int t0, t1;
    if (argc < 2) { printf("usage: PROBE_SCAN spec\n"); return SS$_INSFARG; }

    t0 = now_ms();
    if (!scan(argv[1], 0, &t)) return SS$_ABORT;
    t1 = now_ms();
    printf("search only:   %lu files, %lu dirs, %u ms\n", t.files, t.dirs, t1 - t0);

    t0 = now_ms();
    if (!scan(argv[1], 1, &t)) return SS$_ABORT;
    t1 = now_ms();
    printf("search + ACP:  %lu files, %lu dirs, %lu blocks, %lu ACP failures, %u ms\n",
           t.files, t.dirs, t.blocks, t.acp_fail, t1 - t0);
    return SS$_NORMAL;
}
