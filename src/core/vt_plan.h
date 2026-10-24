/* vt_plan.h - pure helpers for operations: name patterns, names, version room. */
#ifndef VT_PLAN_H
#define VT_PLAN_H

#include <stddef.h>

/* Apply an output pattern as COPY does: "*.BAK", "NEW.*", "X" (no type: keep the
   type). Each field is "*" or a plain name; no other wildcards, no version, device
   or directory. type comes with its dot (".COM"); otype gets one. Returns 1 or 0. */
int vt_name_apply(const char *pattern, const char *name, const char *type,
                  char *oname, size_t onsz, char *otype, size_t otsz);
/* Does the pattern contain a '*' (so it can apply to several files)? */
int vt_pattern_wild(const char *pattern);
/* A name for a new directory: letters, digits, $ _ and -, 1 to 39 characters. */
int vt_valid_dirname(const char *s);

enum { VT_ROOM_OK, VT_ROOM_LIMIT, VT_ROOM_32767 };
/* Can incoming new versions be added to count existing ones (highest h) without
   RMS deleting old ones (limit, 0 or >= 32767 = none) or passing 32767? */
int vt_version_room(int count, int highest, int incoming, int limit);

#endif
