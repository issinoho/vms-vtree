/* vt_util.h - pure helpers: strings, numbers, dates, protection, VMS names. */
#ifndef VT_UTIL_H
#define VT_UTIL_H

#include <stddef.h>
#include "vt_types.h"

/* Copy at most size-1 characters and always terminate. Returns strlen(src). */
size_t vt_strlcpy(char *dst, const char *src, size_t size);
/* Append; same contract as strlcat. */
size_t vt_strlcat(char *dst, const char *src, size_t size);
/* Case-blind comparison (ASCII). */
int vt_strcasecmp(const char *a, const char *b);
int vt_strncasecmp(const char *a, const char *b, size_t n);
/* Case-blind search; returns the match or 0. */
const char *vt_strcasestr(const char *hay, const char *needle);

/* 1234567 -> "1,234,567". Returns buf. */
char *vt_commas(unsigned long n, char *buf, size_t size);

/* "24-OCT-2026 15:02:07.31" (full) or "24-OCT-2026 15:02" (short). 0 -> "". */
char *vt_fmt_time(vt_time t, int full, char *buf, size_t size);
/* Build a vt_time from fields (for tests and the stub). */
vt_time vt_make_time(int year, int month, int day, int hour, int min, int sec);

/* "(S:RWED,O:RWED,G:RE,W)" from a protection word. */
char *vt_fmt_prot(unsigned short prot, char *buf, size_t size);
/* "[200,101]" (octal). */
char *vt_fmt_uic(unsigned int uic, char *buf, size_t size);
/* Record format, organisation and attributes in DIRECTORY/FULL's words. */
const char *vt_rfm_name(unsigned char rfm);
const char *vt_org_name(unsigned char org);
char *vt_fmt_rat(unsigned char rat, char *buf, size_t size);

/* Directory specifications.
   vt_dir_child("[000000]", "USERS") -> "[USERS]";
   vt_dir_child("[USERS]", "IAIN")   -> "[USERS.IAIN]". Returns 0 if too long. */
int vt_dir_child(const char *dir, const char *name, char *out, size_t size);
/* "[USERS.][IAIN]" -> "[USERS.IAIN]"; "<A.B>" -> "[A.B]"; in place. */
void vt_dir_normalise(char *dir);
/* Split "[A.B.C]" into components; returns the count (0 for [000000]).
   Components are copied into comp[i] (each VT_NAME_MAX). */
int vt_dir_split(const char *dir, char comp[][VT_NAME_MAX], int max);

#endif
