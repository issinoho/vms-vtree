/* vt_platform.h - the whole OS surface of VTREE (D3). No VMS types.
   src/plat/vms/ implements it with RMS, ACP QIOs, system services and SMG$;
   src/plat/stub/ with an in-memory file system for the Linux tests.

   Status values are VMS condition values: odd = success. vtp_message() turns one
   into text. Phase 1 has no call that changes a file. */
#ifndef VT_PLATFORM_H
#define VT_PLATFORM_H

#include "vt_types.h"
#include "vt_screen.h"

#define VTP_OK 1u
/* VTREE's own conditions are all even (failures), so "st & 1" stays the test. */
#define VTP_STOPPED 0x1000u   /* a list callback asked to stop (not a VMS code; even = failure) */

typedef struct {
    char user[40];
    unsigned int uic;
    char privs_off[80];       /* the privileges VTREE turned off, "" if none were on */
    int privileged;           /* /PRIVILEGES: left on (D5); 0 if none of them is held */
    char privs_on[80];        /* in privileged mode: which of the four are on */
    char node[16];
} vtp_session;

/* Start: switch off protection-overriding privileges (D5), take the terminal.
   With privileged (the /PRIVILEGES qualifier) they stay as they are, and
   s->privileged says whether any of them is on. Returns a status; on failure
   nothing has been changed. */
unsigned int vtp_start(vtp_session *s, int privileged);
void vtp_stop(void);                        /* give the terminal back */

/* Screen */
void vtp_screen_size(int *rows, int *cols);
void vtp_draw(const vt_screen *s);          /* show the model (only what changed) */
void vtp_repaint(void);                     /* redraw everything (Ctrl/W) */
/* A key, waiting at most timeout seconds (0: only look). VK_TIMEOUT when none. */
int  vtp_key(int timeout);
/* A broadcast message that arrived, copied into buf. 0 when none. */
int  vtp_broadcast(char *buf, int size);

/* Time and messages */
vt_time vtp_now(void);
void vtp_message(unsigned int status, char *buf, int size);
/* User name for a UIC ("IAIN", or "" if it has none). */
void vtp_uic_name(unsigned int uic, char *buf, int size);

/* Devices */
typedef struct {
    char name[64];            /* "DKA0:" */
    char label[16];           /* volume label */
    char logvol[64];          /* "DISK$USER" or "" */
    unsigned int free_blocks, max_blocks;
    int ods;                  /* 2 or 5, 0 if unknown */
} vtp_device;
typedef int (*vtp_device_cb)(void *ctx, const vtp_device *d);
unsigned int vtp_devices(vtp_device_cb cb, void *ctx);      /* mounted disks */
unsigned int vtp_device_info(const char *dev, vtp_device *d);

/* Where the user is: the physical device and directory of SYS$DISK:[], or of spec
   (any file or directory specification; logical names allowed). dir is normalised
   ("[USERS.IAIN]"). Fails if the directory doesn't exist. */
unsigned int vtp_locate(const char *spec, char *dev, int devsz, char *dir, int dirsz);

/* Directory listing. pattern is "*.DIR;1" (subdirectories) or "*.*;*".
   With attrs, each entry's attributes are read without opening the file (N2).
   The callback returns 0 to go on, nonzero to stop (the call then returns
   VTP_STOPPED). Entries come in directory order. */
typedef struct {
    const char *name, *type;  /* "LOGIN", ".COM" */
    unsigned int ver;
    unsigned short fid[3];
    int is_dir;               /* directory bit set (always read for .DIR entries) */
    int is_alias;             /* directory entry whose back-link is not here (N5) */
    int link_unknown;         /* a directory whose back-link could not be read */
    int is_self;              /* the directory's own entry ([000000]000000.DIR) */
    vt_attrs a;               /* a.status = 0 when attrs were not asked for */
} vtp_entry;
typedef int (*vtp_entry_cb)(void *ctx, const vtp_entry *e);
unsigned int vtp_list(const char *dev, const char *dir, const char *pattern, int attrs,
                      vtp_entry_cb cb, void *ctx);

/* Details for the Info screen: one file's security. */
typedef struct {
    char owner[80];           /* "[ISSINOHO,IAIN]" or "[200,101]" */
    int acl_count;
    char acl[32][128];        /* formatted ACEs */
    unsigned int acl_status;  /* 1, or why the ACL couldn't be read */
} vtp_security;
unsigned int vtp_security_get(const char *path, vtp_security *s);

/* Viewing a file, read-only and shared (other users may have it open).
   Record mode: records in order; vtp_view_pos gives a position to come back to.
   Block mode: 512-byte virtual blocks, numbered from 1. */
typedef struct vtp_view vtp_view;
typedef struct { unsigned int a, b; } vtp_pos;
unsigned int vtp_view_open(const char *path, int blocks, vtp_view **v);
/* Next record into buf (cut at size); *len is its full length; *pos where it began.
   Returns RMS$_EOF-like failure (even status) at the end. */
unsigned int vtp_view_next(vtp_view *v, char *buf, int size, int *len, vtp_pos *pos);
unsigned int vtp_view_seek(vtp_view *v, const vtp_pos *pos);
unsigned int vtp_view_block(vtp_view *v, unsigned int vbn, unsigned char *buf, int *len);
void vtp_view_close(vtp_view *v);
int vtp_is_eof(unsigned int status);

/* ---------- Phase 2: changes that lose nothing ---------- */

/* Copy as DCL COPY does (N9, D12): contents, organisation, record attributes and
   creation date; the new file gets the default protection and owner and no ACL.
   dst is "dev:[dir]name.type" without a version: a new highest version is made.
   On failure nothing is left behind. result: the new file's full specification. */
unsigned int vtp_copy(const char *src, const char *dst, char *result, int size);
/* Rename (or move within one disk). to includes the version to give it. */
unsigned int vtp_rename(const char *from, const char *to, char *result, int size);
/* Create a directory (and any missing parents). VTP_CREATED if it was made. */
#define VTP_CREATED 0x691u            /* SS$_CREATED */
unsigned int vtp_mkdir(const char *dev, const char *dir);
/* Versions of dev dir name.type: how many exist, the highest number, and the
   version limit a new version would meet (0 = none). */
unsigned int vtp_versions(const char *dev, const char *dir, const char *name, const char *type,
                          int *count, int *highest, int *limit);
/* Set a file's protection word (S,O,G,W nibbles; a set bit denies R,W,E,D). */
unsigned int vtp_set_prot(const char *path, unsigned short prot);
/* ---------- Phase 3: deleting ---------- */

/* Delete exactly one file: path must name one version (";n"), with no wildcard;
   anything else is refused with VTP_NOTEXACT before RMS is called. A directory file
   is deleted only if it is empty (the file system checks). */
#define VTP_NOTEXACT 0x1004u
unsigned int vtp_delete(const char *path);
/* Compare two files block by block: VTP_OK if identical, VTP_DIFFERENT if not. */
#define VTP_DIFFERENT 0x1006u
unsigned int vtp_compare(const char *a, const char *b);

/* ---------- Phase 4: security and attributes ---------- */

unsigned int vtp_set_owner(const char *path, unsigned int uic);
/* A user name ("IAIN") to its UIC; fails for a name that is not a UIC identifier. */
unsigned int vtp_name_uic(const char *name, unsigned int *uic);
/* ACEs as $FORMAT_ACL writes them, e.g. "(IDENTIFIER=[*,*],ACCESS=READ)". */
unsigned int vtp_acl_add(const char *path, const char *ace);
unsigned int vtp_acl_delete(const char *path, const char *ace);
unsigned int vtp_acl_delete_all(const char *path);
/* 0 = no expiration date. */
unsigned int vtp_set_expiry(const char *path, vt_time t);
/* File characteristics: only these two may be set or cleared; every other bit is
   written back as it was read. */
#define VTP_CH_NOBACKUP (1u << 1)
#define VTP_CH_ERASE    (1u << 17)
unsigned int vtp_set_flags(const char *path, unsigned int set, unsigned int clear);
/* Run a DCL command with no terminal (input and output NL:). In the command, $A and
   $B stand for file_a and file_b, which reach it as job logical names (D6). */
unsigned int vtp_run(const char *command, const char *file_a, const char *file_b);

/* Throw away keys typed ahead, so that they can never answer a question. */
void vtp_flush_keys(void);
/* Append one line to the operation log (VTREE$LOG, default SYS$LOGIN:VTREE.LOG). */
void vtp_log(const char *line);
/* Hand the terminal to a subprocess and take it back: the editor on file
   (VTREE$EDITOR, default EDIT/TPU), or an interactive DCL when file is 0. */
unsigned int vtp_spawn(const char *file);

#endif
