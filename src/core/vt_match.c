/* vt_match.c - VMS wildcards and the filespec filter. No I/O. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "vt_match.h"
#include "vt_util.h"

int vt_wild(const char *p, const char *s)
{
    const char *star = 0, *resume = 0;
    while (*s) {
        if (*p == '*') { star = p++; resume = s; continue; }
        if (*p == '%' || (*p && toupper((unsigned char) *p) == toupper((unsigned char) *s))) {
            p++; s++; continue;
        }
        if (star) { p = star + 1; s = ++resume; continue; }
        return 0;
    }
    while (*p == '*') p++;
    return *p == '\0';
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n && isspace((unsigned char) s[n - 1])) s[--n] = '\0';
    while (s[i] && isspace((unsigned char) s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

/* Find c outside ^ escapes. */
static char *find_unescaped(char *s, char c)
{
    for (; *s; s++) {
        if (*s == '^' && s[1]) { s++; continue; }
        if (*s == c) return s;
    }
    return 0;
}

static int parse_one(vt_pattern *p, char *item)
{
    char *dot, *semi;
    trim(item);
    if (!*item) return 0;
    if (strchr(item, ':') || strchr(item, '[') || strchr(item, '<')) return 0;
    semi = find_unescaped(item, ';');
    p->ver = -1;
    if (semi) {
        char *v = semi + 1, *end;
        *semi = '\0';
        if (*v == '\0' || strcmp(v, "*") == 0) p->ver = -1;
        else {
            long n = strtol(v, &end, 10);
            if (*end || n < 0 || n > 32767) return 0;
            p->ver = (int) n;
        }
    }
    dot = find_unescaped(item, '.');
    if (dot) {
        *dot = '\0';
        vt_strlcpy(p->type, *(dot + 1) ? dot + 1 : "", sizeof p->type);
    } else vt_strlcpy(p->type, "*", sizeof p->type);
    vt_strlcpy(p->name, *item ? item : "*", sizeof p->name);
    return 1;
}

int vt_filter_parse(vt_filter *f, const char *text)
{
    char buf[VT_SPEC_MAX], *item, *comma;
    vt_filter tmp;
    int i;
    memset(&tmp, 0, sizeof tmp);
    vt_strlcpy(buf, text, sizeof buf);
    trim(buf);
    if (!*buf) vt_strlcpy(buf, "*.*;*", sizeof buf);
    item = buf;
    for (;;) {
        comma = find_unescaped(item, ',');
        if (comma) *comma = '\0';
        if (tmp.count == VT_FILTER_MAX || !parse_one(&tmp.p[tmp.count], item)) return 0;
        tmp.count++;
        if (!comma) break;
        item = comma + 1;
    }
    for (i = 0; i < tmp.count; i++) {
        char one[3 * VT_NAME_MAX];
        const vt_pattern *p = &tmp.p[i];
        if (p->ver < 0) snprintf(one, sizeof one, "%s.%s;*", p->name, p->type);
        else snprintf(one, sizeof one, "%s.%s;%d", p->name, p->type, p->ver);
        if (i) vt_strlcat(tmp.text, ",", sizeof tmp.text);
        vt_strlcat(tmp.text, one, sizeof tmp.text);
    }
    *f = tmp;
    return 1;
}

int vt_filter_match(const vt_filter *f, const char *name, const char *type,
                    unsigned int ver, int highest)
{
    int i;
    if (*type == '.') type++;
    for (i = 0; i < f->count; i++) {
        const vt_pattern *p = &f->p[i];
        if (!vt_wild(p->name, name) || !vt_wild(p->type, type)) continue;
        if (p->ver < 0 || (p->ver == 0 && highest) || (p->ver > 0 && (unsigned) p->ver == ver))
            return 1;
    }
    return 0;
}
