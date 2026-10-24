/* vt_plat_stub.c - vt_platform.h over an in-memory file system, for Linux tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vt_platform.h"
#include "vt_stub.h"
#include "vt_util.h"
#include "vt_match.h"

typedef struct {
    char dev[32], dir[VT_SPEC_MAX], name[VT_NAME_MAX], type[VT_NAME_MAX];
    unsigned int ver;
    unsigned short fid[3];
    int is_dir, is_alias, denied;
    vt_attrs a;
    char *content;
    size_t len;
} sfile;

typedef struct { char dev[32], from[VT_SPEC_MAX], to[VT_SPEC_MAX]; } salias;

static sfile *files; static int nfiles, capfiles;
static salias aliases[8]; static int naliases;
static vtp_device devs[8]; static int ndevs;
static char def_dev[32], def_dir[VT_SPEC_MAX];
static int keys[512], nkeys, kpos;
static unsigned short next_fid = 100;

void stub_reset(void)
{
    int i;
    for (i = 0; i < nfiles; i++) free(files[i].content);
    free(files); files = 0; nfiles = capfiles = 0;
    naliases = ndevs = 0; nkeys = kpos = 0; next_fid = 100;
}

void stub_device(const char *dev, const char *label, unsigned int fb, unsigned int mb)
{
    vtp_device *d = &devs[ndevs++];
    memset(d, 0, sizeof *d);
    vt_strlcpy(d->name, dev, sizeof d->name);
    vt_strlcpy(d->label, label, sizeof d->label);
    d->free_blocks = fb; d->max_blocks = mb; d->ods = 5;
}

void stub_default(const char *dev, const char *dir)
{
    vt_strlcpy(def_dev, dev, sizeof def_dev);
    vt_strlcpy(def_dir, dir, sizeof def_dir);
}

static sfile *add(const char *dev, const char *dir, const char *name, const char *type,
                  unsigned int ver)
{
    sfile *f;
    if (nfiles == capfiles) {
        capfiles = capfiles ? capfiles * 2 : 64;
        files = (sfile *) realloc(files, (size_t) capfiles * sizeof *files);
    }
    f = &files[nfiles++];
    memset(f, 0, sizeof *f);
    vt_strlcpy(f->dev, dev, sizeof f->dev);
    vt_strlcpy(f->dir, dir, sizeof f->dir);
    vt_strlcpy(f->name, name, sizeof f->name);
    vt_strlcpy(f->type, type, sizeof f->type);
    f->ver = ver;
    f->fid[0] = next_fid++; f->fid[1] = 1;
    f->a.status = 1;
    f->a.uic = (0200u << 16) | 0101u;
    f->a.prot = 0xFA00;          /* S:RWED,O:RWED,G:RE,W */
    f->a.rfm = VT_RFM_VAR; f->a.rat = 2; f->a.mrs = 255;
    f->a.cdt = f->a.rdt = vt_make_time(2026, 10, 1, 9, 30, 0);
    f->a.revisions = 1;
    return f;
}

static sfile *find(const char *dev, const char *dir, const char *name, const char *type,
                   unsigned int ver)
{
    int i;
    for (i = 0; i < nfiles; i++)
        if (!vt_strcasecmp(files[i].dev, dev) && !vt_strcasecmp(files[i].dir, dir)
            && !vt_strcasecmp(files[i].name, name) && !vt_strcasecmp(files[i].type, type)
            && files[i].ver == ver)
            return &files[i];
    return 0;
}

static int dir_exists(const char *dev, const char *dir)
{
    char comp[64][VT_NAME_MAX], parent[VT_SPEC_MAX], tmp[VT_SPEC_MAX];
    int n = vt_dir_split(dir, comp, 64), i;
    if (n == 0) return 1;
    vt_strlcpy(parent, "[000000]", sizeof parent);
    for (i = 0; i < n - 1; i++) { vt_dir_child(parent, comp[i], tmp, sizeof tmp); vt_strlcpy(parent, tmp, sizeof parent); }
    return find(dev, parent, comp[n - 1], ".DIR", 1) != 0;
}

void stub_dir(const char *dev, const char *dir)
{
    char comp[64][VT_NAME_MAX], parent[VT_SPEC_MAX], tmp[VT_SPEC_MAX];
    int n = vt_dir_split(dir, comp, 64), i;
    if (!find(dev, "[000000]", "000000", ".DIR", 1)) {
        sfile *m = add(dev, "[000000]", "000000", ".DIR", 1);
        m->is_dir = 1; m->fid[0] = 4; m->fid[1] = 4;
    }
    vt_strlcpy(parent, "[000000]", sizeof parent);
    for (i = 0; i < n; i++) {
        if (!find(dev, parent, comp[i], ".DIR", 1)) {
            sfile *d = add(dev, parent, comp[i], ".DIR", 1);
            d->is_dir = 1; d->a.uchar = 1u << 13; d->a.rfm = VT_RFM_VAR; d->a.eof_blocks = 1;
            d->a.alloc_blocks = 1;
        }
        vt_dir_child(parent, comp[i], tmp, sizeof tmp);
        vt_strlcpy(parent, tmp, sizeof parent);
    }
}

void stub_dir_denied(const char *dev, const char *dir)
{
    sfile *m;
    stub_dir(dev, dir);
    m = add(dev, dir, "__DENIED__", "", 0);
    m->denied = 1;
}

void stub_file(const char *dev, const char *dir, const char *spec, unsigned int blocks,
               vt_time rdt, const char *content)
{
    char name[VT_NAME_MAX], *dot, *semi;
    sfile *f;
    vt_strlcpy(name, spec, sizeof name);
    semi = strchr(name, ';');
    *semi = '\0';
    dot = strchr(name, '.');
    {
        char type[VT_NAME_MAX];
        vt_strlcpy(type, dot, sizeof type);
        *dot = '\0';
        f = add(dev, dir, name, type, (unsigned int) atoi(semi + 1));
    }
    f->a.eof_blocks = blocks;
    f->a.alloc_blocks = (blocks + 15) / 16 * 16;
    f->a.ffb = 0;
    if (rdt) f->a.rdt = rdt;
    if (content) {
        f->len = strlen(content);
        f->content = (char *) malloc(f->len + 1);
        memcpy(f->content, content, f->len + 1);
        f->a.eof_blocks = (unsigned int) ((f->len + 511) / 512);
        f->a.ffb = (unsigned short) (f->len % 512);
        if (f->a.ffb == 0 && f->len) f->a.ffb = 0;
    }
}

void stub_alias(const char *dev, const char *in, const char *name, const char *target)
{
    sfile *d = add(dev, in, name, ".DIR", 1);
    salias *al = &aliases[naliases++];
    char from[VT_SPEC_MAX];
    d->is_dir = 1; d->is_alias = 1; d->a.uchar = 1u << 13;
    vt_dir_child(in, name, from, sizeof from);
    vt_strlcpy(al->dev, dev, sizeof al->dev);
    vt_strlcpy(al->from, from, sizeof al->from);
    vt_strlcpy(al->to, target, sizeof al->to);
}

void stub_keys(const int *k)
{
    nkeys = kpos = 0;
    for (; *k != VK_NONE && nkeys < 512; k++) keys[nkeys++] = *k;
}

/* ---------- the platform ---------- */

unsigned int vtp_start(vtp_session *s)
{
    memset(s, 0, sizeof *s);
    vt_strlcpy(s->user, "IAIN", sizeof s->user);
    s->uic = (0200u << 16) | 0101u;
    vt_strlcpy(s->privs_off, "SYSPRV READALL GRPPRV", sizeof s->privs_off);
    vt_strlcpy(s->node, "STUB", sizeof s->node);
    return VTP_OK;
}

void vtp_stop(void) { }
void vtp_screen_size(int *rows, int *cols) { *rows = 24; *cols = 80; }
void vtp_draw(const vt_screen *s) { (void) s; }
void vtp_repaint(void) { }
int vtp_key(int timeout) { (void) timeout; return kpos < nkeys ? keys[kpos++] : VK_TIMEOUT; }
int vtp_broadcast(char *buf, int size) { (void) buf; (void) size; return 0; }
vt_time vtp_now(void) { return vt_make_time(2026, 10, 24, 15, 2, 0); }

void vtp_message(unsigned int st, char *buf, int size)
{
    const char *m = "%NONAME-?-NOMSG, message number";
    if (st & 1) m = "%SYSTEM-S-NORMAL, normal successful completion";
    else if (st == STUB_NOPRIV) m = "%SYSTEM-F-NOPRIV, insufficient privilege or object protection violation";
    else if (st == STUB_DNF) m = "%RMS-E-DNF, directory not found";
    snprintf(buf, (size_t) size, "%s", m);
}

void vtp_uic_name(unsigned int uic, char *buf, int size)
{
    vt_strlcpy(buf, uic == ((0200u << 16) | 0101u) ? "IAIN" : "", (size_t) size);
}

unsigned int vtp_devices(vtp_device_cb cb, void *ctx)
{
    int i;
    for (i = 0; i < ndevs; i++) if (cb(ctx, &devs[i])) break;
    return VTP_OK;
}

unsigned int vtp_device_info(const char *dev, vtp_device *d)
{
    int i;
    for (i = 0; i < ndevs; i++)
        if (!vt_strcasecmp(devs[i].name, dev)) { *d = devs[i]; return VTP_OK; }
    return 0x144u;   /* SS$_NOSUCHDEV */
}

unsigned int vtp_locate(const char *spec, char *dev, int devsz, char *dir, int dirsz)
{
    char d1[32], d2[VT_SPEC_MAX];
    const char *colon;
    if (!spec || !*spec) { vt_strlcpy(d1, def_dev, sizeof d1); vt_strlcpy(d2, def_dir, sizeof d2); }
    else {
        colon = strchr(spec, ':');
        if (!colon) return STUB_DNF;
        snprintf(d1, sizeof d1, "%.*s", (int) (colon - spec + 1), spec);
        vt_strlcpy(d2, colon[1] ? colon + 1 : "[000000]", sizeof d2);
        vt_dir_normalise(d2);
    }
    if (!dir_exists(d1, d2)) return STUB_DNF;
    vt_strlcpy(dev, d1, (size_t) devsz);
    vt_strlcpy(dir, d2, (size_t) dirsz);
    return VTP_OK;
}

static int cmp_sfile(const void *p, const void *q)
{
    const sfile *x = *(const sfile *const *) p, *y = *(const sfile *const *) q;
    int c = vt_strcasecmp(x->name, y->name);
    if (!c) c = vt_strcasecmp(x->type, y->type);
    if (!c) c = (int) y->ver - (int) x->ver;
    return c;
}

unsigned int vtp_list(const char *dev, const char *dir, const char *pattern, int attrs,
                      vtp_entry_cb cb, void *ctx)
{
    char real[VT_SPEC_MAX];
    sfile **sel;
    int n = 0, i, k;
    vt_filter f;
    vt_strlcpy(real, dir, sizeof real);
    for (k = 0; k < naliases; k++) {            /* a path through an alias */
        size_t fl = strlen(aliases[k].from) - 1;      /* without the ] */
        if (!vt_strcasecmp(aliases[k].dev, dev) && !vt_strncasecmp(dir, aliases[k].from, fl)
            && (dir[fl] == ']' || dir[fl] == '.')) {
            snprintf(real, sizeof real, "%.*s%s", (int) strlen(aliases[k].to) - 1,
                     aliases[k].to, dir + fl);
        }
    }
    if (!dir_exists(dev, real)) return STUB_DNF;
    vt_filter_parse(&f, pattern);
    sel = (sfile **) malloc((size_t) (nfiles + 1) * sizeof *sel);
    for (i = 0; i < nfiles; i++) {
        sfile *s = &files[i];
        if (vt_strcasecmp(s->dev, dev) || vt_strcasecmp(s->dir, real)) continue;
        if (s->denied) { free(sel); return STUB_NOPRIV; }
        if (vt_filter_match(&f, s->name, s->type, s->ver, 1)) sel[n++] = s;
    }
    qsort(sel, (size_t) n, sizeof *sel, cmp_sfile);
    for (i = 0; i < n; i++) {
        vtp_entry e;
        memset(&e, 0, sizeof e);
        e.name = sel[i]->name; e.type = sel[i]->type; e.ver = sel[i]->ver;
        memcpy(e.fid, sel[i]->fid, sizeof e.fid);
        e.is_dir = sel[i]->is_dir;
        e.is_alias = sel[i]->is_alias;
        e.is_self = !vt_strcasecmp(real, "[000000]") && !vt_strcasecmp(sel[i]->name, "000000");
        if (attrs) e.a = sel[i]->a;
        if (cb(ctx, &e)) { free(sel); return VTP_STOPPED; }
    }
    free(sel);
    return VTP_OK;
}

static sfile *by_path(const char *path)
{
    int i;
    char buf[3 * VT_SPEC_MAX];
    for (i = 0; i < nfiles; i++) {
        snprintf(buf, sizeof buf, "%s%s%s%s;%u", files[i].dev, files[i].dir, files[i].name,
                 files[i].type, files[i].ver);
        if (!vt_strcasecmp(buf, path)) return &files[i];
    }
    return 0;
}

unsigned int vtp_security_get(const char *path, vtp_security *s)
{
    sfile *f = by_path(path);
    memset(s, 0, sizeof *s);
    if (!f) return 0x910u;   /* SS$_NOSUCHFILE */
    vt_strlcpy(s->owner, "[IAIN]", sizeof s->owner);
    s->acl_status = VTP_OK;
    if (!vt_strcasecmp(f->name, "SECRET")) {
        s->acl_count = 1;
        vt_strlcpy(s->acl[0], "(IDENTIFIER=[*,*],ACCESS=READ)", sizeof s->acl[0]);
    }
    return VTP_OK;
}

struct vtp_view { sfile *f; size_t off; int blocks; };

unsigned int vtp_view_open(const char *path, int blocks, vtp_view **v)
{
    sfile *f = by_path(path);
    if (!f) return 0x910u;
    *v = (vtp_view *) calloc(1, sizeof **v);
    (*v)->f = f; (*v)->blocks = blocks;
    return VTP_OK;
}

unsigned int vtp_view_next(vtp_view *v, char *buf, int size, int *len, vtp_pos *pos)
{
    const char *c = v->f->content, *nl;
    size_t n;
    if (!c || v->off >= v->f->len) return STUB_EOF;
    pos->a = (unsigned int) v->off; pos->b = 0;
    nl = strchr(c + v->off, '\n');
    n = nl ? (size_t) (nl - (c + v->off)) : v->f->len - v->off;
    *len = (int) n;
    if ((int) n >= size) n = (size_t) size - 1;
    memcpy(buf, c + v->off, n);
    buf[n] = '\0';
    v->off += (size_t) *len + (nl ? 1 : 0);
    return VTP_OK;
}

unsigned int vtp_view_seek(vtp_view *v, const vtp_pos *pos) { v->off = pos->a; return VTP_OK; }

unsigned int vtp_view_block(vtp_view *v, unsigned int vbn, unsigned char *buf, int *len)
{
    size_t start = (size_t) (vbn - 1) * 512;
    memset(buf, 0, 512);
    *len = 0;
    if (!v->f->content || start >= v->f->len) return STUB_EOF;
    *len = (int) (v->f->len - start < 512 ? v->f->len - start : 512);
    memcpy(buf, v->f->content + start, (size_t) *len);
    return VTP_OK;
}

void vtp_view_close(vtp_view *v) { free(v); }
int vtp_is_eof(unsigned int st) { return st == STUB_EOF; }
