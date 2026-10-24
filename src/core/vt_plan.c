/* vt_plan.c - pure helpers for operations. No I/O. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "vt_plan.h"
#include "vt_util.h"

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

int vt_version_room(int count, int highest, int incoming, int limit)
{
    if (highest + incoming > 32767) return VT_ROOM_32767;
    if (limit > 0 && limit < 32767 && count + incoming > limit) return VT_ROOM_LIMIT;
    return VT_ROOM_OK;
}
