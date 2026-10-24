/* vt_stub.h - building the stub platform's in-memory file system (Linux tests). */
#ifndef VT_STUB_H
#define VT_STUB_H

#include "vt_types.h"

#define STUB_NOPRIV 0x24u       /* SS$_NOPRIV */
#define STUB_DNF    0x1C04Au    /* RMS$_DNF */
#define STUB_EOF    0x1827Au    /* RMS$_EOF */

void stub_reset(void);
void stub_device(const char *dev, const char *label, unsigned int free_blocks,
                 unsigned int max_blocks);
void stub_default(const char *dev, const char *dir);
/* A directory; its parents are created too. */
void stub_dir(const char *dev, const char *dir);
/* A directory whose listing fails with NOPRIV. */
void stub_dir_denied(const char *dev, const char *dir);
/* name like "LOGIN.COM;3". content may be 0 (then blocks of zeros). */
void stub_file(const char *dev, const char *dir, const char *name, unsigned int blocks,
               vt_time rdt, const char *content);
/* An alias entry: [in]name.DIR;1 is another name for target. */
void stub_alias(const char *dev, const char *in, const char *name, const char *target);
/* The entry [in]name.DIR;1 can't have its header read (back-link unknown, N8). */
void stub_nolink(const char *dev, const char *in, const char *name);
/* A directory's version limit (0 = none). */
void stub_limit(const char *dev, const char *dir, int limit);
/* The next copy of a file with this name fails (STUB_NOPRIV). */
void stub_fail_copy(const char *name);
/* The operation log so far, lines joined with '\n'; and the last spawn's file
   ("DCL" for a subprocess, "" if none). */
const char *stub_log(void);
const char *stub_spawned(void);
#define STUB_FEX 0x18292u       /* RMS$_FEX: file already exists */
/* The next keys vtp_key will return; VK_NONE ends the list. */
void stub_keys(const int *keys);

#endif
