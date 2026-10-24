/* vt_plan.c - pure helpers for operations. No I/O. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "vt_plan.h"
#include "vt_util.h"
#include "vt_types.h"

static int plain(const char *s)
{
    for (; *s; s++) {
        if (*s == '^' && s[1]) { s++; continue; }
        if (strchr("*%;:[]<>,\"/ \t", *s)) return 0;
    }
    return 1;
}

int vt_pattern_wild(const char *p) { return strchr(p, '*') != 0; }

int vt_name_apply(const char *pattern, const char *name, const char *type,
                  char *oname, size_t onsz, char *otype, size_t otsz)
{
    char np[512], tp[512];
    const char *dot = 0, *p;
    for (p = pattern; *p; p++) {
        if (*p == '^' && p[1]) { p++; continue; }
        if (*p == '.') { dot = p; break; }
    }
    if (dot) {
        snprintf(np, sizeof np, "%.*s", (int) (dot - pattern), pattern);
        vt_strlcpy(tp, dot + 1, sizeof tp);
    } else {
        vt_strlcpy(np, pattern, sizeof np);
        vt_strlcpy(tp, "*", sizeof tp);
    }
    if (!np[0]) return 0;
    if (strcmp(np, "*") == 0) vt_strlcpy(oname, name, onsz);
    else if (plain(np)) vt_strlcpy(oname, np, onsz);
    else return 0;
    if (strcmp(tp, "*") == 0) vt_strlcpy(otype, type, otsz);
    else if (plain(tp)) { vt_strlcpy(otype, ".", otsz); vt_strlcat(otype, tp, otsz); }
    else return 0;
    return 1;
}

int vt_valid_dirname(const char *s)
{
    size_t n = strlen(s), i;
    if (n == 0 || n > 39) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        if (!(isalnum(c) || c == '$' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

int vt_purge_select(const vt_nver *f, int n, int keep, int *out)
{
    int i, j, marked = 0;
    if (keep < 1) keep = 1;                   /* never every version */
    for (i = 0; i < n; i++) {
        int higher = 0;
        for (j = 0; j < n; j++)
            if (j != i && f[j].ver > f[i].ver && !vt_strcasecmp(f[j].name, f[i].name)
                && !vt_strcasecmp(f[j].type, f[i].type))
                higher++;
        out[i] = higher >= keep;
        marked += out[i];
    }
    return marked;
}

static void skip_sp(const char **p) { while (**p == ' ' || **p == '\t') (*p)++; }

int vt_parse_prot(const char *text, unsigned short old, unsigned short *out)
{
    const char *p = text;
    unsigned short prot = old;
    int any = 0, paren = 0, closed = 0;
    skip_sp(&p);
    if (*p == '(') { paren = 1; p++; }
    for (;;) {
        int cls = -1;
        unsigned int deny = 0xF;
        skip_sp(&p);
        if (paren && *p == ')') { p++; closed = 1; break; }
        if (!*p) break;
        switch (toupper((unsigned char) *p)) {
        case 'S': cls = 0; break; case 'O': cls = 1; break;
        case 'G': cls = 2; break; case 'W': cls = 3; break;
        default: return 0;
        }
        {   /* the class name, short or long */
            static const char *const names[] = { "SYSTEM", "OWNER", "GROUP", "WORLD" };
            const char *n = names[cls];
            while (*p && isalpha((unsigned char) *p)) {
                if (!*n || toupper((unsigned char) *p) != *n) return 0;
                p++; n++;
            }
        }
        skip_sp(&p);
        if (*p == ':' || *p == '=') {
            p++;
            skip_sp(&p);
            while (*p && strchr("RWEDrwed", *p)) {
                switch (toupper((unsigned char) *p)) {
                case 'R': deny &= ~1u; break; case 'W': deny &= ~2u; break;
                case 'E': deny &= ~4u; break; case 'D': deny &= ~8u; break;
                }
                p++;
            }
        }
        prot = (unsigned short) ((prot & ~(0xFu << (4 * cls))) | (deny << (4 * cls)));
        any = 1;
        skip_sp(&p);
        if (*p == ',') { p++; continue; }
        if (paren && *p == ')') { p++; closed = 1; break; }
        if (*p) return 0;
        break;
    }
    skip_sp(&p);
    if (*p || !any || paren != closed) return 0;
    *out = prot;
    return 1;
}

int vt_parse_uic(const char *text, unsigned int *uic)
{
    const char *p = text;
    unsigned long g = 0, m = 0;
    int gd = 0, md = 0;
    skip_sp(&p);
    if (*p != '[' && *p != '<') return 0;
    p++;
    while (*p >= '0' && *p <= '7') { g = g * 8 + (unsigned long) (*p++ - '0'); gd++; }
    if (*p != ',' || !gd) return 0;
    p++;
    while (*p >= '0' && *p <= '7') { m = m * 8 + (unsigned long) (*p++ - '0'); md++; }
    if ((*p != ']' && *p != '>') || !md || g > 037776 || m > 0177776) return 0;
    p++;
    skip_sp(&p);
    if (*p) return 0;
    *uic = (unsigned int) ((g << 16) | m);
    return 1;
}

int vt_parse_date(const char *text, vt_time *out)
{
    static const char *const mon[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL",
                                       "AUG", "SEP", "OCT", "NOV", "DEC" };
    const char *p = text;
    int d = 0, m = -1, y = 0, hh = 0, mm = 0, ss = 0, i, n;
    static const int mdays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    skip_sp(&p);
    if (vt_strncasecmp(p, "NONE", 4) == 0) {
        p += 4; skip_sp(&p);
        if (*p) return 0;
        *out = 0;
        return 1;
    }
    for (n = 0; *p >= '0' && *p <= '9' && n < 2; n++) d = d * 10 + (*p++ - '0');
    if (!n || *p++ != '-') return 0;
    for (i = 0; i < 12; i++) if (vt_strncasecmp(p, mon[i], 3) == 0) m = i;
    if (m < 0) return 0;
    p += 3;
    if (*p++ != '-') return 0;
    for (n = 0; *p >= '0' && *p <= '9' && n < 4; n++) y = y * 10 + (*p++ - '0');
    if (n != 4 || y < 1859 || y > 9999) return 0;
    if (d < 1 || d > mdays[m] || (m == 1 && d == 29 && !((y % 4 == 0 && y % 100) || y % 400 == 0)))
        return 0;
    if (*p == ' ' || *p == ':') {
        p++;
        for (n = 0; *p >= '0' && *p <= '9' && n < 2; n++) hh = hh * 10 + (*p++ - '0');
        if (!n || *p++ != ':') return 0;
        for (n = 0; *p >= '0' && *p <= '9' && n < 2; n++) mm = mm * 10 + (*p++ - '0');
        if (n != 2) return 0;
        if (*p == ':') {
            p++;
            for (n = 0; *p >= '0' && *p <= '9' && n < 2; n++) ss = ss * 10 + (*p++ - '0');
            if (n != 2) return 0;
        }
        if (hh > 23 || mm > 59 || ss > 59) return 0;
    }
    skip_sp(&p);
    if (*p) return 0;
    *out = vt_make_time(y, m + 1, d, hh, mm, ss);
    return 1;
}

int vt_version_room(int count, int highest, int incoming, int limit)
{
    if (highest + incoming > 32767) return VT_ROOM_32767;
    if (limit > 0 && limit < 32767 && count + incoming > limit) return VT_ROOM_LIMIT;
    return VT_ROOM_OK;
}
