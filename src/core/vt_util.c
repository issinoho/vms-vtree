/* vt_util.c - pure helpers. No I/O. */
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "vt_util.h"

size_t vt_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (size) {
        size_t c = n < size - 1 ? n : size - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}

size_t vt_strlcat(char *dst, const char *src, size_t size)
{
    size_t d = strlen(dst);
    if (d >= size) return d + strlen(src);
    return d + vt_strlcpy(dst + d, src, size - d);
}

int vt_strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int ca = toupper((unsigned char) *a), cb = toupper((unsigned char) *b);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
    return 0;
}

int vt_strcasecmp(const char *a, const char *b)
{
    return vt_strncasecmp(a, b, (size_t) -1);
}

const char *vt_strcasestr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return hay;
    for (; *hay; hay++)
        if (vt_strncasecmp(hay, needle, n) == 0) return hay;
    return 0;
}

char *vt_commas(unsigned long n, char *buf, size_t size)
{
    char tmp[32];
    int len, i, o = 0, lead;
    len = sprintf(tmp, "%lu", n);
    lead = len % 3;
    for (i = 0; i < len && (size_t) o + 2 < size; i++) {
        if (i && (i - lead) % 3 == 0) buf[o++] = ',';
        buf[o++] = tmp[i];
    }
    buf[o] = '\0';
    return buf;
}

/* Days since 1970-01-01 to y/m/d (H. Hinnant's civil_from_days). */
static void civil(long z, int *y, int *m, int *d)
{
    long era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int) (doy - (153 * mp + 2) / 5 + 1);
    *m = (int) (mp < 10 ? mp + 3 : mp - 9);
    *y = (int) (yoe + era * 400 + (*m <= 2));
}

static long days_from_civil(int y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

#define VT_TICKS_PER_DAY 864000000000ULL
#define VT_MJD_1970 40587L          /* 17-NOV-1858 is MJD 0 */

static const char *const months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

char *vt_fmt_time(vt_time t, int full, char *buf, size_t size)
{
    unsigned long long days, rem;
    int y, m, d, hh, mm, ss, cc;
    if (t == 0) { vt_strlcpy(buf, "", size); return buf; }
    days = t / VT_TICKS_PER_DAY;
    rem = t % VT_TICKS_PER_DAY;
    civil((long) days - VT_MJD_1970, &y, &m, &d);
    rem /= 100000ULL;                       /* hundredths of a second */
    cc = (int) (rem % 100); rem /= 100;
    ss = (int) (rem % 60); rem /= 60;
    mm = (int) (rem % 60); hh = (int) (rem / 60);
    if (full)
        snprintf(buf, size, "%2d-%s-%04d %02d:%02d:%02d.%02d", d, months[m - 1], y,
                 hh, mm, ss, cc);
    else
        snprintf(buf, size, "%2d-%s-%04d %02d:%02d", d, months[m - 1], y, hh, mm);
    return buf;
}

vt_time vt_make_time(int year, int month, int day, int hour, int min, int sec)
{
    long days = days_from_civil(year, month, day) + VT_MJD_1970;
    return (vt_time) days * VT_TICKS_PER_DAY
         + ((vt_time) ((hour * 60 + min) * 60 + sec)) * 10000000ULL;
}

char *vt_fmt_prot(unsigned short prot, char *buf, size_t size)
{
    static const char cls[] = "SOGW";
    char out[48];
    int c, o = 0;
    out[o++] = '(';
    for (c = 0; c < 4; c++) {
        unsigned int deny = (prot >> (4 * c)) & 0xF;
        if (c) out[o++] = ',';
        out[o++] = cls[c];
        if (deny != 0xF) {
            out[o++] = ':';
            if (!(deny & 1)) out[o++] = 'R';
            if (!(deny & 2)) out[o++] = 'W';
            if (!(deny & 4)) out[o++] = 'E';
            if (!(deny & 8)) out[o++] = 'D';
        }
    }
    out[o++] = ')';
    out[o] = '\0';
    vt_strlcpy(buf, out, size);
    return buf;
}

char *vt_fmt_uic(unsigned int uic, char *buf, size_t size)
{
    snprintf(buf, size, "[%o,%o]", uic >> 16, uic & 0xFFFF);
    return buf;
}

const char *vt_rfm_name(unsigned char rfm)
{
    static const char *const n[] = { "Undefined", "Fixed length", "Variable length",
        "VFC", "Stream", "Stream_LF", "Stream_CR" };
    return rfm < 7 ? n[rfm] : "Unknown";
}

const char *vt_org_name(unsigned char org)
{
    return org == VT_ORG_SEQ ? "Sequential" : org == VT_ORG_REL ? "Relative"
         : org == VT_ORG_IDX ? "Indexed" : "Unknown";
}

char *vt_fmt_rat(unsigned char rat, char *buf, size_t size)
{
    /* FAT$V_FORTRANCC 0, IMPLIEDCC 1, PRINTCC 2, NOSPAN 3, MSBRCW 4 */
    buf[0] = '\0';
    if (rat & 2) vt_strlcat(buf, "Carriage return carriage control", size);
    else if (rat & 1) vt_strlcat(buf, "Fortran carriage control", size);
    else if (rat & 4) vt_strlcat(buf, "Print file carriage control", size);
    else vt_strlcat(buf, "None", size);
    if (rat & 8) vt_strlcat(buf, ", Non-spanned", size);
    return buf;
}

int vt_dir_child(const char *dir, const char *name, char *out, size_t size)
{
    size_t dl = strlen(dir);
    if (dl < 2) return 0;
    if (vt_strcasecmp(dir, "[000000]") == 0)
        return (size_t) snprintf(out, size, "[%s]", name) < size;
    return (size_t) snprintf(out, size, "%.*s.%s]", (int) (dl - 1), dir, name) < size;
}

void vt_dir_normalise(char *dir)
{
    char *p, *o;
    for (p = o = dir; *p; ) {
        if (*p == '^' && p[1]) { *o++ = *p++; *o++ = *p++; continue; }
        if (*p == '<') { *o++ = '['; p++; continue; }
        if (*p == '>') { *o++ = ']'; p++; continue; }
        if (p[0] == '.' && (p[1] == ']' || p[1] == '>') && (p[2] == '[' || p[2] == '<')) {
            *o++ = '.'; p += 3; continue;
        }
        *o++ = *p++;
    }
    *o = '\0';
    /* "[000000.A]" can arrive from a root of a rooted device: drop the 000000. */
    if (strncmp(dir, "[000000.", 8) == 0) memmove(dir + 1, dir + 8, strlen(dir + 8) + 1);
}

int vt_dir_split(const char *dir, char comp[][VT_NAME_MAX], int max)
{
    const char *p = dir;
    int n = 0, len = 0;
    if (*p == '[' || *p == '<') p++;
    if (vt_strncasecmp(p, "000000]", 7) == 0 || vt_strncasecmp(p, "000000>", 7) == 0)
        return 0;
    for (; *p && n < max; p++) {
        if (*p == '^' && p[1]) {
            if (len < VT_NAME_MAX - 2) { comp[n][len++] = p[0]; comp[n][len++] = p[1]; }
            p++;
            continue;
        }
        if (*p == '.' || *p == ']' || *p == '>') {
            comp[n][len] = '\0';
            if (len) n++;
            len = 0;
            if (*p != '.') break;
            continue;
        }
        if (len < VT_NAME_MAX - 1) comp[n][len++] = *p;
    }
    return n;
}
