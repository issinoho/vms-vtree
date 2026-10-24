/* vt_plat_vms.c - vt_platform.h on OpenVMS (x86-64 and IA64): SMG$ for the screen
   and keys, RMS $SEARCH (NAML: ODS-5 long names) for listings, ACP IO$_ACCESS for
   attributes without opening files (N2), $GET/$FORMAT_ACL for security (N4), RMS
   record and block reads for the viewer. Nothing here writes to a file. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <rms.h>
#include <starlet.h>
#include <descrip.h>
#include <iodef.h>
#include <ssdef.h>
#include <fibdef.h>
#include <atrdef.h>
#include <prvdef.h>
#include <jpidef.h>
#include <syidef.h>
#include <dvidef.h>
#include <dvsdef.h>
#include <dcdef.h>
#include <ossdef.h>
#include <smgdef.h>
#include <smgmsg.h>
#include <smg$routines.h>
#include <lib$routines.h>
#include <gen64def.h>
#include "vt_platform.h"
#include "vt_util.h"
#include "vt_ansi.h"

typedef struct { unsigned short buflen, code; void *buf; unsigned short *retlen; } ile3;
typedef struct { unsigned short size, type; void *addr; } atr_item;

static void ile(ile3 *it, unsigned short code, void *buf, unsigned short len, unsigned short *rl)
{
    it->buflen = len; it->code = code; it->buf = buf; it->retlen = rl;
}

static void dsc(struct dsc$descriptor_s *d, const char *s, unsigned short len)
{
    d->dsc$w_length = len;
    d->dsc$b_dtype = DSC$K_DTYPE_T;
    d->dsc$b_class = DSC$K_CLASS_S;
    d->dsc$a_pointer = (char *) s;
}

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = '\0';
}

/* ---------- session, privileges, terminal ---------- */

/* SMG$ owns the keyboard and traps broadcasts; the screen itself is written by
   vt_ansi through a channel to the terminal, so it can be in colour (D11). */
static unsigned int pb, kb;
static unsigned short ttchan;
static int scr_rows, scr_cols, started, colour = 1;
static vt_cell *shown;              /* what the terminal shows now */
static int shown_rows, shown_cols;

static void tt_write(void *ctx, const char *bytes, int len)
{
    unsigned short iosb[4];
    (void) ctx;
    while (len > 0) {
        int n = len > 2048 ? 2048 : len;
        sys$qiow(0, ttchan, IO$_WRITEVBLK | IO$M_NOFORMAT, (void *) iosb, 0, 0,
                 (void *) bytes, n, 0, 0, 0, 0);
        bytes += n; len -= n;
    }
}

static struct { unsigned int link; void (*handler)(void); unsigned int argc; unsigned int *status;
                unsigned int st; } exit_block;

static void on_exit(void) { vtp_stop(); }

unsigned int vtp_start(vtp_session *s)
{
    union prvdef off, prev;
    unsigned int st, uic = 0;
    char user[13] = "", node[16] = "";
    unsigned short ul = 0, nl = 0, ql = 0;
    ile3 it[3];
    unsigned int one = 1;
    int rows, cols;
    struct dsc$descriptor_s ttd;
    const char *mode;

    memset(s, 0, sizeof *s);
    /* D5: protection must apply. Temporary: image rundown restores them (N7). */
    memset(&off, 0, sizeof off);
    off.prv$v_bypass = off.prv$v_sysprv = off.prv$v_readall = off.prv$v_grpprv = 1;
    memset(&prev, 0, sizeof prev);
    st = sys$setprv(0, (struct _generic_64 *) &off, 0, (struct _generic_64 *) &prev);
    if (!(st & 1)) return st;
    if (prev.prv$v_bypass) vt_strlcat(s->privs_off, "BYPASS ", sizeof s->privs_off);
    if (prev.prv$v_sysprv) vt_strlcat(s->privs_off, "SYSPRV ", sizeof s->privs_off);
    if (prev.prv$v_readall) vt_strlcat(s->privs_off, "READALL ", sizeof s->privs_off);
    if (prev.prv$v_grpprv) vt_strlcat(s->privs_off, "GRPPRV ", sizeof s->privs_off);
    rtrim(s->privs_off);

    ile(&it[0], JPI$_USERNAME, user, 12, &ul);
    ile(&it[1], JPI$_UIC, &uic, 4, &ql);
    memset(&it[2], 0, sizeof it[2]);
    sys$getjpiw(0, 0, 0, it, 0, 0, 0);
    user[ul] = '\0'; rtrim(user);
    vt_strlcpy(s->user, user, sizeof s->user);
    s->uic = uic;
    ile(&it[0], SYI$_NODENAME, node, 15, &nl);
    memset(&it[1], 0, sizeof it[1]);
    sys$getsyiw(0, 0, 0, it, 0, 0, 0);
    node[nl] = '\0';
    vt_strlcpy(s->node, node, sizeof s->node);

    dsc(&ttd, "TT:", 3);
    st = sys$assign(&ttd, &ttchan, 0, 0);
    if (!(st & 1)) return st;
    st = smg$create_pasteboard(&pb, 0, &rows, &cols, 0, 0, 0);
    if (!(st & 1)) { sys$dassgn(ttchan); return st; }
    st = smg$create_virtual_keyboard(&kb, 0, 0, 0, 0);
    if (!(st & 1)) { smg$delete_pasteboard(&pb, &one); sys$dassgn(ttchan); return st; }
    smg$set_broadcast_trapping(&pb, 0, 0);       /* messages queue; shown in the menu line */
    mode = getenv("VTREE$DISPLAY");               /* MONO for a terminal without colour */
    colour = !(mode && (mode[0] == 'M' || mode[0] == 'm'));
    scr_rows = rows; scr_cols = cols;
    started = 1;
    vt_ansi_begin(colour, tt_write, 0);

    exit_block.handler = on_exit;
    exit_block.argc = 1;
    exit_block.status = &exit_block.st;
    sys$dclexh((void *) &exit_block);
    return SS$_NORMAL;
}

void vtp_stop(void)
{
    unsigned int one = 1;
    if (!started) return;
    started = 0;
    vt_ansi_end(tt_write, 0);
    smg$disable_broadcast_trapping(&pb);
    smg$delete_pasteboard(&pb, &one);          /* clears the screen */
    smg$delete_virtual_keyboard(&kb);
    sys$dassgn(ttchan);
    free(shown); shown = 0;
}

void vtp_screen_size(int *rows, int *cols) { *rows = scr_rows; *cols = scr_cols; }

void vtp_draw(const vt_screen *s)
{
    if (!started) return;
    if (!shown || shown_rows != s->rows || shown_cols != s->cols) {
        free(shown);
        shown = (vt_cell *) malloc((size_t) s->rows * (size_t) s->cols * sizeof *shown);
        shown_rows = s->rows; shown_cols = s->cols;
        vt_ansi_render(s, 0, colour, tt_write, 0);          /* everything */
        if (shown) memcpy(shown, s->cell, (size_t) s->rows * (size_t) s->cols * sizeof *shown);
        return;
    }
    vt_ansi_render(s, shown, colour, tt_write, 0);           /* what changed */
}

void vtp_repaint(void) { free(shown); shown = 0; }          /* next draw sends it all */

int vtp_key(int timeout)
{
    unsigned short term = 0;
    unsigned int st;
    if (!started) return VK_TIMEOUT;
    st = smg$read_keystroke(&kb, &term, 0, &timeout, 0, 0, 0);
    if (st == SS$_TIMEOUT) return VK_TIMEOUT;
    if (st == SMG$_EOF) return VK_CTRL_Z;
    if (!(st & 1)) return VK_TIMEOUT;
    switch (term) {
    case SMG$K_TRM_UP: return VK_UP;
    case SMG$K_TRM_DOWN: return VK_DOWN;
    case SMG$K_TRM_LEFT: return VK_LEFT;
    case SMG$K_TRM_RIGHT: return VK_RIGHT;
    case SMG$K_TRM_PF1: return VK_PF1;
    case SMG$K_TRM_PF2: return VK_PF2;
    case SMG$K_TRM_PF3: return VK_PF3;
    case SMG$K_TRM_PF4: return VK_PF4;
    case SMG$K_TRM_FIND: return VK_FIND;
    case SMG$K_TRM_INSERT_HERE: return VK_INSERT;
    case SMG$K_TRM_REMOVE: return VK_REMOVE;
    case SMG$K_TRM_SELECT: return VK_SELECT;
    case SMG$K_TRM_PREV_SCREEN: return VK_PREV;
    case SMG$K_TRM_NEXT_SCREEN: return VK_NEXT;
    case SMG$K_TRM_HELP: return VK_HELP;
    case SMG$K_TRM_DO: return VK_DO;
    case SMG$K_TRM_F10: return VK_F10;
    case SMG$K_TRM_ENTER: return VK_ENTER;
    case SMG$K_TRM_MINUS: return VK_KP_MINUS;
    case SMG$K_TRM_COMMA: return VK_KP_COMMA;
    case SMG$K_TRM_PERIOD: return VK_KP_PERIOD;
    default: break;
    }
    if (term >= SMG$K_TRM_KP0 && term <= SMG$K_TRM_KP9) return VK_KP0 + (term - SMG$K_TRM_KP0);
    if (term < 256) return term;
    return VK_OTHER;
}

int vtp_broadcast(char *buf, int size)
{
    struct dsc$descriptor_s d;
    unsigned short len = 0;
    unsigned int st;
    if (!started) return 0;
    dsc(&d, buf, (unsigned short) (size - 1));
    st = smg$get_broadcast_message(&pb, &d, &len, 0);
    if (st != SS$_NORMAL || len == 0) return 0;
    buf[len] = '\0';
    {   /* one line: control characters (CR, LF, bell) become spaces */
        int i;
        for (i = 0; i < len; i++) if ((unsigned char) buf[i] < 32) buf[i] = ' ';
    }
    return 1;
}

/* ---------- time, messages, names ---------- */

vt_time vtp_now(void)
{
    struct _generic_64 t;
    vt_time v;
    sys$gettim(&t);
    memcpy(&v, &t, sizeof v);
    return v;
}

void vtp_message(unsigned int st, char *buf, int size)
{
    struct dsc$descriptor_s d;
    unsigned short len = 0;
    dsc(&d, buf, (unsigned short) (size - 1));
    if (!(sys$getmsg(st, &len, &d, 15, 0) & 1)) { snprintf(buf, (size_t) size, "%%X%08X", st); return; }
    buf[len] = '\0';
}

void vtp_uic_name(unsigned int uic, char *buf, int size)
{
    static unsigned int cache_uic[16]; static char cache_name[16][40]; static int ncache, next;
    struct dsc$descriptor_s d;
    char name[64];
    unsigned short len = 0;
    int i;
    for (i = 0; i < ncache; i++)
        if (cache_uic[i] == uic) { vt_strlcpy(buf, cache_name[i], (size_t) size); return; }
    /* As DIRECTORY shows an owner: "GROUP,MEMBER", or the member's name alone. */
    dsc(&d, name, sizeof name - 1);
    if (sys$idtoasc(uic, &len, &d, 0, 0, 0) & 1) name[len] = '\0'; else name[0] = '\0';
    if (name[0] && (uic & 0xFFFF) != 0xFFFF) {
        char grp[40];
        unsigned short gl = 0;
        dsc(&d, grp, sizeof grp - 1);
        if (sys$idtoasc(uic | 0xFFFF, &gl, &d, 0, 0, 0) & 1) {
            char both[80];
            grp[gl] = '\0';
            snprintf(both, sizeof both, "%s,%s", grp, name);
            vt_strlcpy(name, both, sizeof name);
        }
    }
    i = ncache < 16 ? ncache++ : (next++ % 16);
    cache_uic[i] = uic;
    vt_strlcpy(cache_name[i], name, sizeof cache_name[i]);
    vt_strlcpy(buf, name, (size_t) size);
}

/* ---------- devices ---------- */

static void strip_underscore(char *s) { if (s[0] == '_') memmove(s, s + 1, strlen(s)); }

unsigned int vtp_device_info(const char *dev, vtp_device *out)
{
    struct dsc$descriptor_s d;
    unsigned int st, free_b = 0, max_b = 0, mnt = 0;
    unsigned int acp = 0;
    char vol[13] = "", lv[65] = "", name[65] = "";
    unsigned short vl = 0, ll = 0, nl = 0, w = 0;
    unsigned short iosb[4];
    ile3 it[8];
    memset(out, 0, sizeof *out);
    dsc(&d, dev, (unsigned short) strlen(dev));
    ile(&it[0], DVI$_FREEBLOCKS, &free_b, 4, &w);
    ile(&it[1], DVI$_MAXBLOCK, &max_b, 4, &w);
    ile(&it[2], DVI$_VOLNAM, vol, 12, &vl);
    ile(&it[3], DVI$_LOGVOLNAM, lv, 64, &ll);
    ile(&it[4], DVI$_MNT, &mnt, 4, &w);
    ile(&it[5], DVI$_ACPTYPE, &acp, 4, &w);
    ile(&it[6], DVI$_DEVNAM, name, 64, &nl);
    memset(&it[7], 0, sizeof it[7]);
    st = sys$getdviw(0, 0, &d, it, (void *) iosb, 0, 0, 0, 0);
    if (st & 1) st = iosb[0];
    if (!(st & 1)) return st;
    vol[vl] = '\0'; rtrim(vol);
    lv[ll] = '\0'; name[nl] = '\0';
    strip_underscore(name);
    vt_strlcpy(out->name, name[0] ? name : dev, sizeof out->name);
    vt_strlcpy(out->label, mnt ? vol : "(not mounted)", sizeof out->label);
    vt_strlcpy(out->logvol, lv, sizeof out->logvol);
    out->free_blocks = mnt ? free_b : 0;
    out->max_blocks = max_b;
    out->ods = acp == DVI$C_ACP_F11V5 ? 5 : acp == DVI$C_ACP_F11V2 ? 2 : 0;
    return mnt ? SS$_NORMAL : SS$_DEVNOTMOUNT;
}

unsigned int vtp_devices(vtp_device_cb cb, void *ctx)
{
    struct dsc$descriptor_s ret, pat;
    char name[65];
    unsigned short len = 0;
    unsigned int cls = DC$_DISK, st;
    struct _generic_64 context;
    ile3 it[2];
    memset(&context, 0, sizeof context);
    dsc(&pat, "*", 1);
    ile(&it[0], DVS$_DEVCLASS, &cls, 4, 0);
    memset(&it[1], 0, sizeof it[1]);
    for (;;) {
        vtp_device dv;
        dsc(&ret, name, sizeof name - 1);
        st = sys$device_scan(&ret, &len, &pat, it, &context);
        if (!(st & 1)) break;
        name[len] = '\0';
        if (vtp_device_info(name, &dv) & 1)          /* mounted disks only */
            if (cb(ctx, &dv)) break;
    }
    return st == SS$_NOMOREDEV || (st & 1) ? SS$_NORMAL : st;
}

/* ---------- where am I ---------- */

static void naml_setup(struct FAB *fab, struct NAML *naml, const char *spec, char *esa, char *rsa)
{
    *fab = cc$rms_fab;
    *naml = cc$rms_naml;
    fab->fab$l_fna = (char *) -1;
    fab->fab$b_fns = 0;
    fab->fab$l_naml = naml;
    naml->naml$l_long_filename = (char *) spec;
    naml->naml$l_long_filename_size = (unsigned int) strlen(spec);
    naml->naml$l_long_expand = esa;
    naml->naml$l_long_expand_alloc = NAML$C_MAXRSS;
    if (rsa) {
        naml->naml$l_long_result = rsa;
        naml->naml$l_long_result_alloc = NAML$C_MAXRSS;
    }
}

unsigned int vtp_locate(const char *spec, char *dev, int devsz, char *dir, int dirsz)
{
    struct FAB fab;
    struct NAML naml;
    static char esa[NAML$C_MAXRSS + 1];
    char tmp[VT_SPEC_MAX];
    unsigned int st;
    naml_setup(&fab, &naml, spec && *spec ? spec : "[]", esa, 0);
    naml.naml$b_nop |= NAML$M_NOCONCEAL;               /* the physical device (N6) */
    st = sys$parse(&fab, 0, 0);
    if (!(st & 1)) return st;
    snprintf(tmp, sizeof tmp, "%.*s", (int) naml.naml$l_long_dev_size, naml.naml$l_long_dev);
    strip_underscore(tmp);
    vt_strlcpy(dev, tmp, (size_t) devsz);
    snprintf(tmp, sizeof tmp, "%.*s", (int) naml.naml$l_long_dir_size, naml.naml$l_long_dir);
    vt_dir_normalise(tmp);
    vt_strlcpy(dir, tmp, (size_t) dirsz);
    /* release RMS's context for this parse */
    naml.naml$b_nop |= NAML$M_SYNCHK;
    sys$parse(&fab, 0, 0);
    return SS$_NORMAL;
}

/* ---------- listing ---------- */

static unsigned int read_attrs(unsigned short chan, const unsigned short fid[3], int full,
                               vt_attrs *a, unsigned int *uchar, unsigned short back[3])
{
    struct fibdef fib;
    struct dsc$descriptor_s fibd;
    unsigned short iosb[4], uic_w[2] = { 0, 0 }, fpro = 0;
    unsigned char fat[ATR$S_RECATTR], asc[ATR$S_ASCDATES];
    unsigned int st, cdt[2], rdt[2], edt[2], bdt[2];
    atr_item atr[12];
    int n = 0;

    memset(&fib, 0, sizeof fib);
    memcpy(&fib.fib$w_fid, fid, 6);
    dsc(&fibd, (char *) &fib, sizeof fib);
    atr[n].size = 4; atr[n].type = ATR$C_UCHAR; atr[n++].addr = uchar;
    atr[n].size = 6; atr[n].type = ATR$C_BACKLINK; atr[n++].addr = back;
    if (full) {
        atr[n].size = ATR$S_RECATTR; atr[n].type = ATR$C_RECATTR; atr[n++].addr = fat;
        atr[n].size = 4; atr[n].type = ATR$C_UIC; atr[n++].addr = uic_w;
        atr[n].size = 2; atr[n].type = ATR$C_FPRO; atr[n++].addr = &fpro;
        atr[n].size = 8; atr[n].type = ATR$C_CREDATE; atr[n++].addr = cdt;
        atr[n].size = 8; atr[n].type = ATR$C_REVDATE; atr[n++].addr = rdt;
        atr[n].size = 8; atr[n].type = ATR$C_EXPDATE; atr[n++].addr = edt;
        atr[n].size = 8; atr[n].type = ATR$C_BAKDATE; atr[n++].addr = bdt;
        atr[n].size = ATR$S_ASCDATES; atr[n].type = ATR$C_ASCDATES; atr[n++].addr = asc;
    }
    atr[n].size = 0; atr[n].type = 0; atr[n].addr = 0;
    st = sys$qiow(0, chan, IO$_ACCESS, (void *) iosb, 0, 0, &fibd, 0, 0, 0, atr, 0);
    if (st & 1) st = iosb[0];
    if (!full) return st;
    memset(a, 0, sizeof *a);
    a->status = st;
    if (!(st & 1)) return st;
    {
        /* FAT: block numbers high word first (N2) */
        const unsigned short *w = (const unsigned short *) fat;
        unsigned int hi = ((unsigned int) w[2] << 16) | w[3];
        unsigned int ef = ((unsigned int) w[4] << 16) | w[5];
        a->rfm = fat[0] & 0x0F; a->org = fat[0] >> 4; a->rat = fat[1];
        a->alloc_blocks = hi;
        a->ffb = w[6];
        a->eof_blocks = (a->ffb == 0 && ef > 0) ? ef - 1 : ef;   /* as DIRECTORY counts */
        a->lrl = w[1];
        a->vfc = fat[15];
        a->mrs = w[8];
        a->defext = w[9];
        a->versions = w[15];
    }
    a->uic = ((unsigned int) uic_w[1] << 16) | uic_w[0];
    a->prot = fpro;
    a->uchar = *uchar;
    memcpy(&a->cdt, cdt, 8); memcpy(&a->rdt, rdt, 8);
    memcpy(&a->edt, edt, 8); memcpy(&a->bdt, bdt, 8);
    a->revisions = (unsigned short) (asc[0] | (asc[1] << 8));
    return st;
}

unsigned int vtp_list(const char *dev, const char *dir, const char *pattern, int attrs,
                      vtp_entry_cb cb, void *ctx)
{
    struct FAB fab;
    struct NAML naml;
    static char esa[NAML$C_MAXRSS + 1], rsa[NAML$C_MAXRSS + 1];
    char spec[VT_SPEC_MAX * 2 + VT_NAME_MAX];
    char name[VT_NAME_MAX], type[VT_NAME_MAX];
    unsigned short chan = 0;
    unsigned int st, ret = SS$_NORMAL;

    snprintf(spec, sizeof spec, "%s%s%s", dev, dir, pattern);
    naml_setup(&fab, &naml, spec, esa, rsa);
    st = sys$parse(&fab, 0, 0);
    if (!(st & 1)) return st;
    while ((st = sys$search(&fab, 0, 0)) & 1) {
        vtp_entry e;
        unsigned int uchar = 0;
        unsigned short back[3] = { 0, 0, 0 };
        int is_dirtype;
        memset(&e, 0, sizeof e);
        snprintf(name, sizeof name, "%.*s", (int) naml.naml$l_long_name_size, naml.naml$l_long_name);
        snprintf(type, sizeof type, "%.*s", (int) naml.naml$l_long_type_size, naml.naml$l_long_type);
        e.name = name; e.type = type;
        {   /* the version field is not terminated: ";12" then whatever follows */
            char v[8];
            unsigned int vl = naml.naml$l_long_ver_size;
            if (vl > 7) vl = 7;
            memcpy(v, naml.naml$l_long_ver, vl);
            v[vl] = '\0';
            e.ver = (unsigned int) atoi(v[0] == ';' || v[0] == '.' ? v + 1 : v);
        }
        memcpy(e.fid, naml.naml$w_fid, 6);
        is_dirtype = vt_strcasecmp(type, ".DIR") == 0;
        if (!chan) {
            struct dsc$descriptor_s dv;
            dsc(&dv, (char *) &naml.naml$t_dvi[1], naml.naml$t_dvi[0]);
            if (!((st = sys$assign(&dv, &chan, 0, 0)) & 1)) { ret = st; break; }
        }
        if (attrs || is_dirtype) {
            unsigned int qs = read_attrs(chan, e.fid, attrs, &e.a, &uchar, back);
            if (qs & 1) {
                e.is_dir = is_dirtype && e.ver == 1 && (uchar & (1u << 13));
                e.is_alias = e.is_dir && memcmp(back, naml.naml$w_did, 6) != 0;
            } else if (is_dirtype && e.ver == 1) {
                e.is_dir = 1;         /* can't read its header: trust the name, */
                e.link_unknown = 1;   /* but it may be an alias (N8) */
            }
        }
        e.is_self = memcmp(e.fid, naml.naml$w_did, 6) == 0;
        if (cb(ctx, &e)) { ret = VTP_STOPPED; break; }
    }
    if (ret == SS$_NORMAL && st != RMS$_NMF && st != RMS$_FNF) ret = st;
    if (st & 1 || ret == VTP_STOPPED) {          /* stopped early: release the search */
        naml.naml$b_nop |= NAML$M_SYNCHK;
        sys$parse(&fab, 0, 0);
    }
    if (chan) sys$dassgn(chan);
    return ret;
}

/* ---------- security ---------- */

unsigned int vtp_security_get(const char *path, vtp_security *s)
{
    struct dsc$descriptor_s cls, obj, ad, td;
    unsigned int owner = 0, ctx = 0, st;
    unsigned short ol = 0, al = 0, tl;
    char ace[256], text[512], name[64];
    ile3 it[2];

    memset(s, 0, sizeof *s);
    dsc(&cls, "FILE", 4);
    dsc(&obj, path, (unsigned short) strlen(path));
    ile(&it[0], OSS$_OWNER, &owner, 4, &ol);
    memset(&it[1], 0, sizeof it[1]);
    st = sys$get_security(&cls, &obj, 0, 0, it, 0, 0);
    if (!(st & 1)) return st;
    vtp_uic_name(owner, name, sizeof name);
    if (name[0]) snprintf(s->owner, sizeof s->owner, "[%s]", name);
    else vt_fmt_uic(owner, s->owner, sizeof s->owner);

    s->acl_status = SS$_NORMAL;
    while (s->acl_count < 32) {
        ile(&it[0], OSS$_ACL_READ_ENTRY, ace, sizeof ace, &al);
        memset(&it[1], 0, sizeof it[1]);
        st = sys$get_security(&cls, &obj, 0, 0, it, &ctx, 0);
        if (!(st & 1)) {
            if (st != SS$_NOMOREACE && st != SS$_ACLEMPTY) s->acl_status = st;
            break;
        }
        dsc(&ad, ace, al);
        dsc(&td, text, sizeof text - 1);
        tl = 0;
        if (sys$format_acl(&ad, &tl, &td, 0, 0, 0, 0) & 1) {
            text[tl] = '\0';
            vt_strlcpy(s->acl[s->acl_count++], text, sizeof s->acl[0]);
        }
    }
    if (ctx) {                                   /* N4: release with OSS$M_RELCTX */
        memset(&it[0], 0, sizeof it[0]);
        sys$get_security(&cls, &obj, 0, OSS$M_RELCTX, it, &ctx, 0);
    }
    return SS$_NORMAL;
}

/* ---------- the viewer ---------- */

struct vtp_view {
    struct FAB fab;
    struct RAB rab;
    struct NAML naml;
    char esa[NAML$C_MAXRSS + 1];
    char path[VT_SPEC_MAX];
};

unsigned int vtp_view_open(const char *path, int blocks, vtp_view **out)
{
    vtp_view *v = (vtp_view *) calloc(1, sizeof *v);
    unsigned int st;
    if (!v) return SS$_INSFMEM;
    vt_strlcpy(v->path, path, sizeof v->path);
    naml_setup(&v->fab, &v->naml, v->path, v->esa, 0);
    v->fab.fab$b_fac = blocks ? (FAB$M_GET | FAB$M_BIO) : FAB$M_GET;
    /* Share with every kind of writer: the file may be in use (read-only here). */
    v->fab.fab$b_shr = FAB$M_SHRGET | FAB$M_SHRPUT | FAB$M_SHRUPD | FAB$M_SHRDEL;
    if (blocks) v->fab.fab$b_shr |= FAB$M_UPI;    /* block I/O with sharing needs it */
    st = sys$open(&v->fab, 0, 0);
    if (!(st & 1)) { free(v); return st; }
    v->rab = cc$rms_rab;
    v->rab.rab$l_fab = &v->fab;
    v->rab.rab$b_rac = RAB$C_SEQ;
    v->rab.rab$l_rop = RAB$M_RRL | RAB$M_NLK;     /* never lock a record */
    if (blocks) v->rab.rab$l_rop |= RAB$M_BIO;
    st = sys$connect(&v->rab, 0, 0);
    if (!(st & 1)) { sys$close(&v->fab, 0, 0); free(v); return st; }
    *out = v;
    return SS$_NORMAL;
}

unsigned int vtp_view_next(vtp_view *v, char *buf, int size, int *len, vtp_pos *pos)
{
    unsigned int st;
    v->rab.rab$b_rac = RAB$C_SEQ;
    v->rab.rab$l_ubf = buf;
    v->rab.rab$w_usz = (unsigned short) (size > 65535 ? 65535 : size);
    st = sys$get(&v->rab, 0, 0);
    if (st == RMS$_RTB) { *len = (int) v->rab.rab$l_stv; st = SS$_NORMAL; }  /* cut short */
    else *len = v->rab.rab$w_rsz;
    if (!(st & 1)) return st;
    pos->a = v->rab.rab$w_rfa[0] | ((unsigned int) v->rab.rab$w_rfa[1] << 16);
    pos->b = v->rab.rab$w_rfa[2];
    return st;
}

unsigned int vtp_view_seek(vtp_view *v, const vtp_pos *pos)
{
    unsigned int st;
    v->rab.rab$b_rac = RAB$C_RFA;
    v->rab.rab$w_rfa[0] = (unsigned short) (pos->a & 0xFFFF);
    v->rab.rab$w_rfa[1] = (unsigned short) (pos->a >> 16);
    v->rab.rab$w_rfa[2] = (unsigned short) pos->b;
    st = sys$find(&v->rab, 0, 0);                 /* the next $GET returns this record */
    v->rab.rab$b_rac = RAB$C_SEQ;
    return st;
}

unsigned int vtp_view_block(vtp_view *v, unsigned int vbn, unsigned char *buf, int *len)
{
    unsigned int st;
    v->rab.rab$l_bkt = vbn;
    v->rab.rab$l_ubf = (char *) buf;
    v->rab.rab$w_usz = 512;
    st = sys$read(&v->rab, 0, 0);
    *len = (st & 1) ? v->rab.rab$w_rsz : 0;
    return st;
}

void vtp_view_close(vtp_view *v)
{
    if (!v) return;
    sys$disconnect(&v->rab, 0, 0);
    sys$close(&v->fab, 0, 0);
    free(v);
}

int vtp_is_eof(unsigned int st) { return st == RMS$_EOF; }

/* ---------- Phase 2: copy, rename, make directory, versions, log, spawn ---------- */

static unsigned int fat_blocks(const unsigned char *fat, unsigned int *ef, unsigned short *ffb)
{
    const unsigned short *w = (const unsigned short *) fat;
    *ef = ((unsigned int) w[4] << 16) | w[5];
    *ffb = w[6];
    return (*ffb == 0 && *ef > 0) ? *ef - 1 : *ef;   /* blocks that hold data */
}

/* Block copy with the source's record attributes written over the new file's at the
   end, as COPY does (N9). The new file is created as plain sequential/undefined with
   UFO so RMS writes nothing into it; every block, an indexed file's prologue
   included, comes from the source. */
unsigned int vtp_copy(const char *src, const char *dst, char *result, int size)
{
    static char sesa[NAML$C_MAXRSS + 1], srsa[NAML$C_MAXRSS + 1];
    static char desa[NAML$C_MAXRSS + 1], drsa[NAML$C_MAXRSS + 1];
    static char buf[64 * 512];
    struct FAB sf, df;
    struct NAML sn, dn;
    struct RAB sr;
    struct XABDAT xd;
    unsigned char fat[ATR$S_RECATTR];
    unsigned int cdt[2] = { 0, 0 }, st, nblocks, vbn, ef;
    unsigned short ffb, chan = 0, dchan, iosb[4];
    atr_item atr[3];
    struct fibdef fib;
    struct dsc$descriptor_s fibd, dv;

    naml_setup(&sf, &sn, src, sesa, srsa);
    sf.fab$b_fac = FAB$M_GET | FAB$M_BIO;
    sf.fab$b_shr = FAB$M_SHRGET;               /* nobody may be writing it meanwhile */
    st = sys$open(&sf, 0, 0);
    if (!(st & 1)) return st;
    sr = cc$rms_rab;
    sr.rab$l_fab = &sf;
    sr.rab$l_rop = RAB$M_BIO;
    st = sys$connect(&sr, 0, 0);
    if (!(st & 1)) { sys$close(&sf, 0, 0); return st; }

    dsc(&dv, (char *) &sn.naml$t_dvi[1], sn.naml$t_dvi[0]);
    st = sys$assign(&dv, &chan, 0, 0);
    if (st & 1) {
        memset(&fib, 0, sizeof fib);
        memcpy(&fib.fib$w_fid, sn.naml$w_fid, 6);
        dsc(&fibd, (char *) &fib, sizeof fib);
        atr[0].size = ATR$S_RECATTR; atr[0].type = ATR$C_RECATTR; atr[0].addr = fat;
        atr[1].size = 8; atr[1].type = ATR$C_CREDATE; atr[1].addr = cdt;
        atr[2].size = 0; atr[2].type = 0; atr[2].addr = 0;
        st = sys$qiow(0, chan, IO$_ACCESS, (void *) iosb, 0, 0, &fibd, 0, 0, 0, atr, 0);
        if (st & 1) st = iosb[0];
        sys$dassgn(chan);
    }
    if (!(st & 1)) { sys$close(&sf, 0, 0); return st; }
    nblocks = fat_blocks(fat, &ef, &ffb);

    naml_setup(&df, &dn, dst, desa, drsa);
    xd = cc$rms_xabdat;
    memcpy(&xd.xab$q_cdt, cdt, 8);             /* COPY keeps the creation date */
    df.fab$l_xab = (void *) &xd;
    df.fab$l_fop = FAB$M_UFO;
    df.fab$b_fac = FAB$M_PUT;
    df.fab$b_org = FAB$C_SEQ;
    df.fab$b_rfm = FAB$C_UDF;
    df.fab$l_alq = nblocks;
    st = sys$create(&df, 0, 0);
    if (!(st & 1)) { sys$close(&sf, 0, 0); return st; }
    dchan = (unsigned short) df.fab$l_stv;

    for (vbn = 1; (st & 1) && vbn <= nblocks; ) {
        unsigned int n = nblocks - vbn + 1, got;
        if (n > 64) n = 64;
        sr.rab$l_bkt = vbn;
        sr.rab$l_ubf = buf;
        sr.rab$w_usz = (unsigned short) (n * 512);
        st = sys$read(&sr, 0, 0);
        if (!(st & 1)) break;
        got = sr.rab$w_rsz;
        if (got == 0) { st = RMS$_EOF; break; }
        if (got % 512) { memset(buf + got, 0, 512 - got % 512); got += 512 - got % 512; }
        st = sys$qiow(0, dchan, IO$_WRITEVBLK, (void *) iosb, 0, 0, buf, got, vbn, 0, 0, 0);
        if (st & 1) st = iosb[0];
        vbn += got / 512;
    }
    if (st & 1) {
        /* the source's attributes, with this file's own allocation */
        unsigned short *w = (unsigned short *) fat;
        w[2] = (unsigned short) (df.fab$l_alq >> 16);
        w[3] = (unsigned short) (df.fab$l_alq & 0xFFFF);
        atr[0].size = ATR$S_RECATTR; atr[0].type = ATR$C_RECATTR; atr[0].addr = fat;
        atr[1].size = 0; atr[1].type = 0; atr[1].addr = 0;
        st = sys$qiow(0, dchan, IO$_DEACCESS, (void *) iosb, 0, 0, 0, 0, 0, 0, atr, 0);
        if (st & 1) st = iosb[0];
    } else {
        unsigned int keep = st;
        sys$qiow(0, dchan, IO$_DEACCESS, (void *) iosb, 0, 0, 0, 0, 0, 0, 0, 0);
        st = keep;
    }
    sys$dassgn(dchan);
    sys$close(&sf, 0, 0);
    drsa[dn.naml$l_long_result_size] = '\0';
    if (!(st & 1)) {                           /* leave nothing half-copied behind */
        struct FAB ef_ = cc$rms_fab;
        struct NAML en;
        static char eesa[NAML$C_MAXRSS + 1];
        naml_setup(&ef_, &en, drsa, eesa, 0);
        sys$erase(&ef_, 0, 0);
        return st;
    }
    vt_strlcpy(result, drsa, (size_t) size);
    return SS$_NORMAL;
}

unsigned int vtp_rename(const char *from, const char *to, char *result, int size)
{
    static char oesa[NAML$C_MAXRSS + 1], orsa[NAML$C_MAXRSS + 1];
    static char nesa[NAML$C_MAXRSS + 1], nrsa[NAML$C_MAXRSS + 1];
    struct FAB of, nf;
    struct NAML on, nn;
    unsigned int st;
    naml_setup(&of, &on, from, oesa, orsa);
    naml_setup(&nf, &nn, to, nesa, nrsa);
    st = sys$rename(&of, 0, 0, &nf);
    if (!(st & 1)) return st;
    nrsa[nn.naml$l_long_result_size] = '\0';
    vt_strlcpy(result, nrsa, (size_t) size);
    return st;
}

unsigned int vtp_mkdir(const char *dev, const char *dir)
{
    char spec[VT_SPEC_MAX * 2];
    struct dsc$descriptor_s d;
    snprintf(spec, sizeof spec, "%s%s", dev, dir);
    dsc(&d, spec, (unsigned short) strlen(spec));
    return lib$create_dir(&d, 0, 0, 0, 0, 0);
}

typedef struct { int count, highest; } vcount;
static int vcount_cb(void *ctx, const vtp_entry *e)
{
    vcount *c = (vcount *) ctx;
    c->count++;
    if ((int) e->ver > c->highest) c->highest = (int) e->ver;
    return 0;
}

unsigned int vtp_versions(const char *dev, const char *dir, const char *name, const char *type,
                          int *count, int *highest, int *limit)
{
    char pat[VT_NAME_MAX * 2 + 4], spec[VT_SPEC_MAX * 3];
    static char esa[NAML$C_MAXRSS + 1];
    vcount c;
    unsigned int st;
    struct FAB fab;
    struct NAML naml;
    unsigned short chan = 0, iosb[4], back[3];
    struct dsc$descriptor_s dv;

    c.count = 0; c.highest = 0;
    snprintf(pat, sizeof pat, "%s%s;*", name, type);
    st = vtp_list(dev, dir, pat, 0, vcount_cb, &c);
    if (!(st & 1) && st != VTP_OK) return st;
    *count = c.count; *highest = c.highest; *limit = 0;

    /* The limit: the existing entry's (FIB$W_VERLIMIT from a lookup), else the
       directory's default (its FAT$W_VERSIONS). */
    snprintf(spec, sizeof spec, "%s%s%s%s;", dev, dir, name, type);
    naml_setup(&fab, &naml, spec, esa, 0);
    st = sys$parse(&fab, 0, 0);
    if (!(st & 1)) return st;
    dsc(&dv, (char *) &naml.naml$t_dvi[1], naml.naml$t_dvi[0]);
    st = sys$assign(&dv, &chan, 0, 0);
    if (st & 1) {
        struct fibdef fib;
        struct dsc$descriptor_s fibd, nd;
        char nm[VT_NAME_MAX * 2 + 4];
        memset(&fib, 0, sizeof fib);
        memcpy(&fib.fib$w_did, naml.naml$w_did, 6);
        dsc(&fibd, (char *) &fib, sizeof fib);
        snprintf(nm, sizeof nm, "%s%s;0", name, type);
        dsc(&nd, nm, (unsigned short) strlen(nm));
        if (c.count && (sys$qiow(0, chan, IO$_ACCESS, (void *) iosb, 0, 0, &fibd, &nd, 0, 0, 0, 0) & 1)
            && (iosb[0] & 1)) {
            *limit = fib.fib$w_verlimit;
        } else {
            unsigned int uchar;
            vt_attrs a;
            if (read_attrs(chan, naml.naml$w_did, 1, &a, &uchar, back) & 1) *limit = a.versions;
        }
        sys$dassgn(chan);
    }
    naml.naml$b_nop |= NAML$M_SYNCHK;
    sys$parse(&fab, 0, 0);
    return SS$_NORMAL;
}

void vtp_log(const char *line)
{
    const char *name = getenv("VTREE$LOG");
    FILE *f = fopen(name && *name ? name : "SYS$LOGIN:VTREE.LOG", "a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

unsigned int vtp_spawn(const char *file)
{
    char lnm[40], cmd[300];
    const char *editor = getenv("VTREE$EDITOR");
    struct dsc$descriptor_s dl, dv, dt, dc, dp;
    unsigned int st, status = 0;
    int have_lnm = 0;

    /* The file goes to the subprocess as a job logical name, so no file name is
       ever written into a command line (D6). */
    sprintf(lnm, "VTREE$FILE_%X", (unsigned int) getpid());
    dsc(&dl, lnm, (unsigned short) strlen(lnm));
    dsc(&dt, "LNM$JOB", 7);
    if (file) {
        dsc(&dv, file, (unsigned short) strlen(file));
        st = lib$set_logical(&dl, &dv, &dt, 0, 0);
        if (!(st & 1)) return st;
        have_lnm = 1;
        snprintf(cmd, sizeof cmd, "%s %s", editor && *editor ? editor : "EDIT/TPU", lnm);
    }
    vt_ansi_end(tt_write, 0);
    smg$disable_broadcast_trapping(&pb);
    dsc(&dc, cmd, (unsigned short) strlen(cmd));
    dsc(&dp, "VTREE_$ ", 8);
    if (!file) {
        static const char hello[] = "\r\nVTREE: a DCL subprocess. LOGOUT brings you back.\r\n";
        tt_write(0, hello, (int) sizeof hello - 1);
    }
    st = lib$spawn(file ? &dc : 0, 0, 0, 0, 0, 0, &status, 0, 0, 0, &dp, 0, 0);
    if (have_lnm) lib$delete_logical(&dl, &dt);
    smg$set_broadcast_trapping(&pb, 0, 0);
    vt_ansi_begin(colour, tt_write, 0);
    free(shown); shown = 0;                    /* the next draw sends the whole screen */
    if (!(st & 1)) return st;
    return (status & 1) || !file ? SS$_NORMAL : status;
}

unsigned int vtp_set_prot(const char *path, unsigned short prot)
{
    struct dsc$descriptor_s cls, obj;
    unsigned int p = prot;
    unsigned short pl = 0;
    ile3 it[2];
    dsc(&cls, "FILE", 4);
    dsc(&obj, path, (unsigned short) strlen(path));
    ile(&it[0], OSS$_PROTECTION, &p, 2, &pl);     /* 2 bytes, not 4 (N4) */
    memset(&it[1], 0, sizeof it[1]);
    return sys$set_security(&cls, &obj, 0, 0, it, 0, 0);
}

void vtp_flush_keys(void)
{
    int n = 0;
    while (started && n++ < 512 && vtp_key(0) != VK_TIMEOUT) ;
}
