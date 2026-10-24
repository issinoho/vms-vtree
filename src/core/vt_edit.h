/* vt_edit.h - the one-line editor used by every prompt (DCL-style keys). */
#ifndef VT_EDIT_H
#define VT_EDIT_H

#include "vt_types.h"

typedef struct {
    char buf[VT_SPEC_MAX];
    int len, pos;
    int overstrike;              /* Ctrl/A toggles, as in DCL */
} vt_edit;

enum { VT_EDIT_MORE, VT_EDIT_DONE, VT_EDIT_CANCEL };

void vt_edit_init(vt_edit *e, const char *initial);
/* Feed one key. Return toggles done; F10 and Ctrl/Z cancel.
   Left/right, Ctrl/H (start), Ctrl/E (end), Delete (rub out), Ctrl/U (erase to start),
   Ctrl/A (insert/overstrike). */
int vt_edit_key(vt_edit *e, int key);

#endif
