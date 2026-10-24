/* vt_plan.h - pure helpers for operations: name patterns, names, version room. */
#ifndef VT_PLAN_H
#define VT_PLAN_H

#include <stddef.h>
#include "vt_types.h"

/* Apply an output pattern as COPY does: "*.BAK", "NEW.*", "X" (no type: keep the
   type). Each field is "*" or a plain name; no other wildcards, no version, device
   or directory. type comes with its dot (".COM"); otype gets one. Returns 1 or 0. */
int vt_name_apply(const char *pattern, const char *name, const char *type,
                  char *oname, size_t onsz, char *otype, size_t otsz);
/* Does the pattern contain a '*' (so it can apply to several files)? */
int vt_pattern_wild(const char *pattern);
/* A name for a new directory: letters, digits, $ _ and -, 1 to 39 characters. */
int vt_valid_dirname(const char *s);

/* Purge: given one directory's files as name/type/version triples, mark (out[i] = 1)
   every version beyond the keep highest of its name.type. Order of the input is
   free. Returns how many were marked. keep >= 1. */
typedef struct { const char *name, *type; unsigned int ver; } vt_nver;
int vt_purge_select(const vt_nver *f, int n, int keep, int *out);

/* Protection in DCL syntax: "(S:RWED,O:RWED,G:RE,W)", "G:R,W", "SYSTEM:RWED,OWNER:RW".
   Classes not named keep their old value; "W" or "W:" alone means no access.
   Returns 1 and the new word in *out, or 0 if the text is not valid. */
int vt_parse_prot(const char *text, unsigned short old, unsigned short *out);
/* "[200,101]" (octal) into a UIC. 1 if it was a numeric UIC, 0 otherwise (a name:
   the platform looks it up). */
int vt_parse_uic(const char *text, unsigned int *uic);
/* "1-JAN-2030", "1-JAN-2030 14:30", "1-jan-2030 14:30:15", or "NONE" (0).
   Returns 1 and the time, or 0. */
int vt_parse_date(const char *text, vt_time *out);

enum { VT_ROOM_OK, VT_ROOM_LIMIT, VT_ROOM_32767 };
/* Can incoming new versions be added to count existing ones (highest h) without
   RMS deleting old ones (limit, 0 or >= 32767 = none) or passing 32767? */
int vt_version_room(int count, int highest, int incoming, int limit);

#endif
