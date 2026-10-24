/* vt_tree.h - the directory tree (read one level at a time, D7) and each directory's
   file list. Nodes live in one array and refer to each other by index. */
#ifndef VT_TREE_H
#define VT_TREE_H

#include <stddef.h>
#include "vt_types.h"

/* File flags */
#define VT_F_TAGGED  1
#define VT_F_DIR     2      /* a directory file (FCH$V_DIRECTORY) */
#define VT_F_ALIAS   4      /* a directory entry whose back-link is elsewhere (D8) */
#define VT_F_HIGHEST 8      /* highest version of its name.type */

typedef struct {
    char *name;             /* "LOGIN" (escaped as RMS returns it) */
    char *type;             /* ".COM" */
    unsigned short ver;
    unsigned short fid[3];
    unsigned char flags;
    vt_attrs a;
} vt_file;

typedef struct {
    vt_file *f;
    int n, cap;
    int loaded;             /* read at least once */
    int complete;           /* the read ran to the end (not stopped by a key) */
    unsigned int status;    /* status of the read: 1 = fine */
} vt_flist;

/* Node flags */
#define VT_N_ROOT    1
#define VT_N_SCANNED 2      /* subdirectories read */
#define VT_N_OPEN    4      /* children shown */
#define VT_N_ALIAS   8      /* its directory entry is an alias (D8) */
#define VT_N_ERROR  16      /* reading it failed: see scan_status */
#define VT_N_NOLINK 32      /* its back-link could not be read: treated as an alias */
#define VT_N_CLOSED (VT_N_ALIAS | VT_N_NOLINK)   /* a recursive command never passes it */

typedef struct {
    char *name;             /* directory name, or the device ("DKA0:") for a root */
    int parent, child, next;
    int depth;
    unsigned char flags;
    unsigned short fid[3];
    unsigned int scan_status;
    vt_flist files;
} vt_node;

typedef struct {
    vt_node *n;
    int count, cap;
    int first_root;
} vt_tree;

void vt_tree_init(vt_tree *t);
void vt_tree_free(vt_tree *t);
/* Add (or find) the root for a device. Returns its index, -1 on no memory. */
int  vt_tree_root(vt_tree *t, const char *device);
/* Add (or find, case-blind) a child; children are kept in name order.
   alias: 0 a plain directory, 1 an alias (D8), 2 back-link unknown. */
int  vt_tree_child(vt_tree *t, int parent, const char *name, const unsigned short fid[3],
                   int alias);
int  vt_tree_find_child(const vt_tree *t, int parent, const char *name);
/* The device ("DKA0:") and directory ("[USERS.IAIN]", "[000000]") of a node. */
void vt_tree_spec(const vt_tree *t, int node, char *dev, size_t devsz, char *dir, size_t dirsz);
/* The nodes on screen, in order: roots and the children of open nodes.
   Returns the count; fills out[] up to max. */
int  vt_tree_visible(const vt_tree *t, int *out, int max);
/* Is node the last of its siblings? */
int  vt_tree_is_last(const vt_tree *t, int node);
/* Is a an ancestor of (or equal to) b? */
int  vt_tree_within(const vt_tree *t, int a, int b);
/* Is b within a, with no alias or unchecked entry on the way down (a itself may be
   one)? This is what a recursive command started at a may touch (D8). */
int  vt_tree_reaches(const vt_tree *t, int a, int b);
/* The highest node from which node is reachable in that sense. */
int  vt_tree_reach_top(const vt_tree *t, int node);

/* Give a node a new name, keeping its siblings in name order. 0 on no memory. */
int  vt_tree_rename(vt_tree *t, int node, const char *name);

/* File lists */
int  vt_flist_add(vt_flist *fl, const char *name, const char *type, unsigned int ver,
                  const unsigned short fid[3], unsigned char flags, const vt_attrs *a);
void vt_flist_clear(vt_flist *fl);
/* Set VT_F_HIGHEST on the first (directory order: highest) of each name.type. */
void vt_flist_mark_highest(vt_flist *fl);

#endif
