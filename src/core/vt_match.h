/* vt_match.h - VMS wildcards and the file window's filespec filter. */
#ifndef VT_MATCH_H
#define VT_MATCH_H

#include "vt_types.h"

/* Case-blind match of s against pattern: '*' any run, '%' exactly one character. */
int vt_wild(const char *pattern, const char *s);

#define VT_FILTER_MAX 16

typedef struct {
    char name[VT_NAME_MAX];
    char type[VT_NAME_MAX];      /* without the dot */
    int ver;                     /* -1 = any (;* or none), 0 = highest only, n = ;n */
} vt_pattern;

typedef struct {
    int count;
    vt_pattern p[VT_FILTER_MAX];
    char text[VT_SPEC_MAX];      /* as typed, normalised: "*.C;*,*.H;*" */
} vt_filter;

/* Parse "*.C;*, *.H" (comma list). A missing type means "*", a missing version
   means every version, as DIRECTORY does. Returns 1, or 0 if the text is invalid
   (empty item, too many items, or a device or directory in it). */
int vt_filter_parse(vt_filter *f, const char *text);
/* type is with or without its leading dot; highest says whether ver is the highest
   version of this name.type in its directory. */
int vt_filter_match(const vt_filter *f, const char *name, const char *type,
                    unsigned int ver, int highest);

#endif
