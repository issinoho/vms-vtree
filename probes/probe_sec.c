/* probe_sec.c - Phase 0 probe 3: file security and privileges, own files only.

   1. Turn off the privileges that override file protection (BYPASS, SYSPRV,
      READALL, GRPPRV) in this process only and print which had been on. VTREE
      would do this at start-up so protection always applies (D5).
   2. On a scratch file it creates in the current directory (PROBE_SEC.TMP):
      $GET_SECURITY protection and owner; $SET_SECURITY a new protection;
      $PARSE_ACL an ACE and add it with OSS$_ACL_ADD_ENTRY; read the ACL back;
      then delete the file. Nothing outside the scratch file is touched. */
#include <stdio.h>
#include <string.h>
#include <starlet.h>
#include <descrip.h>
#include <prvdef.h>
#include <ossdef.h>
#include <ssdef.h>
#include "probe_common.h"

static struct dsc$descriptor_s cls, obj;

static void show(const char *when)
{
    unsigned int prot = 0, owner = 0, ctx = 0, st;
    unsigned short pl = 0, ol = 0, al = 0;
    char ace[256], text[512];
    ile3 it[3];
    struct dsc$descriptor_s ad, td;
    unsigned short tl;

    ile3_set(&it[0], OSS$_PROTECTION, &prot, 4, &pl);
    ile3_set(&it[1], OSS$_OWNER, &owner, 4, &ol);
    ile3_end(&it[2]);
    st = sys$get_security(&cls, &obj, 0, 0, it, 0, 0);
    printf("%s: get_security %s prot=%04X owner=[%o,%o]\n", when,
           (st & 1) ? "ok" : msg(st), prot, owner >> 16, owner & 0xffff);

    for (;;) {                       /* one ACE per call; context keeps the place */
        ile3_set(&it[0], OSS$_ACL_READ_ENTRY, ace, sizeof ace, &al);
        ile3_end(&it[1]);
        st = sys$get_security(&cls, &obj, 0, 0, it, &ctx, 0);
        if (!(st & 1)) break;
        desc_set(&ad, ace, al);
        desc_set(&td, text, sizeof text - 1);
        tl = 0;
        if (sys$format_acl(&ad, &tl, &td, 0, 0, 0, 0) & 1) {
            text[tl] = '\0';
            printf("  ACE %s\n", text);
        }
    }
    if (st != SS$_NOMOREACE && st != SS$_ACLEMPTY) printf("  ACL read ended: %s\n", msg(st));
    if (ctx) {                       /* release the context: flag, empty item list */
        ile3_end(&it[0]);
        sys$get_security(&cls, &obj, 0, OSS$M_RELCTX, it, &ctx, 0);
    }
}

int main(void)
{
    union prvdef off, prev;
    unsigned int st, prot;
    unsigned short pl;
    char ace[256];
    unsigned short al = 0;
    struct dsc$descriptor_s acetext, aced;
    ile3 it[2];
    FILE *f;

    memset(&off, 0, sizeof off);
    off.prv$v_bypass = off.prv$v_sysprv = off.prv$v_readall = off.prv$v_grpprv = 1;
    memset(&prev, 0, sizeof prev);
    st = sys$setprv(0, (struct _generic_64 *) &off, 0, (struct _generic_64 *) &prev);
    printf("setprv off: %s; were on: BYPASS=%d SYSPRV=%d READALL=%d GRPPRV=%d\n",
           msg(st), prev.prv$v_bypass, prev.prv$v_sysprv, prev.prv$v_readall,
           prev.prv$v_grpprv);

    f = fopen("PROBE_SEC.TMP", "w");
    if (!f) { printf("cannot create scratch file\n"); return SS$_ABORT; }
    fputs("scratch\n", f);
    fclose(f);

    desc_set(&cls, "FILE", 4);
    desc_set(&obj, "PROBE_SEC.TMP", 13);
    show("before");

    prot = 0xFF00;     /* S:RWED O:RWED G: W: (a set bit denies) */
    /* $SET_SECURITY wants exactly 2 bytes here (4 gives IVBUFLEN; $GET accepts 4). */
    ile3_set(&it[0], OSS$_PROTECTION, &prot, 2, &pl);
    ile3_end(&it[1]);
    st = sys$set_security(&cls, &obj, 0, 0, it, 0, 0);
    printf("set_security protection: %s\n", msg(st));

    desc_set(&acetext, "(IDENTIFIER=[*,*],ACCESS=READ)", 30);
    desc_set(&aced, ace, sizeof ace);
    st = sys$parse_acl(&acetext, &aced, 0, 0, 0);
    al = (unsigned char) ace[0];          /* first byte of an ACE is its length */
    printf("parse_acl: %s (ACE %u bytes)\n", msg(st), al);
    if (st & 1) {
        ile3_set(&it[0], OSS$_ACL_ADD_ENTRY, ace, al, 0);
        ile3_end(&it[1]);
        st = sys$set_security(&cls, &obj, 0, 0, it, 0, 0);
        printf("set_security add ACE: %s\n", msg(st));
    }
    show("after");

    printf("delete scratch: %s\n", remove("PROBE_SEC.TMP") == 0 ? "ok" : "FAILED");
    return SS$_NORMAL;
}
