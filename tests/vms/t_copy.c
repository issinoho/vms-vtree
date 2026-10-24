/* t_copy.c - vtp_copy, vtp_versions and vtp_rename on the node, in [.COPYTEST] only.
   Run after probes/copy_setup.com; then @copy_compare VT compares with the source. */
#include <stdio.h>
#include <string.h>
#include "vt_platform.h"

static const char *files[] = { "VFC.TXT", "VAR.TXT", "STREAM.TXT", "INDEXED.DAT",
                               "IMAGE.EXE", "EMPTY.DAT", 0 };

int main(void)
{
    char dev[256], dir[256], src[512], dst[512], res[512], m[256];
    unsigned int st;
    int i, count, highest, limit;
    st = vtp_locate("[.COPYTEST.VT]", dev, sizeof dev, dir, sizeof dir);
    vtp_message(st, m, sizeof m);
    printf("locate %s %s: %s\n", dev, dir, m);
    for (i = 0; files[i]; i++) {
        snprintf(src, sizeof src, "[.COPYTEST.SRC]%s;1", files[i]);
        snprintf(dst, sizeof dst, "%s%s%s", dev, dir, files[i]);
        st = vtp_copy(src, dst, res, sizeof res);
        vtp_message(st, m, sizeof m);
        printf("copy %-12s -> %s: %s\n", files[i], (st & 1) ? res : "-", m);
    }
    /* a second copy makes version 2 */
    st = vtp_copy("[.COPYTEST.SRC]VAR.TXT;1", "[.COPYTEST.VT]AGAIN.TXT", res, sizeof res);
    st = vtp_copy("[.COPYTEST.SRC]VAR.TXT;1", "[.COPYTEST.VT]AGAIN.TXT", res, sizeof res);
    printf("again: %s\n", res);
    st = vtp_versions(dev, dir, "AGAIN", ".TXT", &count, &highest, &limit);
    printf("versions AGAIN.TXT: count %d highest %d limit %d\n", count, highest, limit);
    st = vtp_versions(dev, dir, "NEW", ".TXT", &count, &highest, &limit);
    printf("versions NEW.TXT: count %d highest %d limit %d\n", count, highest, limit);
    /* a copy from a file that does not exist leaves nothing */
    st = vtp_copy("[.COPYTEST.SRC]NONE.TXT;1", "[.COPYTEST.VT]NONE.TXT", res, sizeof res);
    vtp_message(st, m, sizeof m);
    printf("missing source: %s\n", m);
    st = vtp_rename("[.COPYTEST.VT]AGAIN.TXT;1", "[.COPYTEST.VT]RENAMED.TXT;1", res, sizeof res);
    vtp_message(st, m, sizeof m);
    printf("rename: %s %s\n", m, (st & 1) ? res : "");
    /* a new directory inside the test tree */
    {
        char nd[300];
        snprintf(nd, sizeof nd, "%.*s.NEWDIR]", (int) strlen(dir) - 1, dir);
        st = vtp_mkdir(dev, nd);
    }
    vtp_message(st, m, sizeof m);
    printf("mkdir: %s\n", m);
    return 1;
}
