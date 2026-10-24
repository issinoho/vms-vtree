/* vt_main.c - VTREE [/PRIVILEGES] [start-directory]   (run as a foreign command)

   /PRIVILEGES (D5): leave BYPASS, SYSPRV, READALL and GRPPRV as they are, for a system
   manager who means to work past file protection. Only at start-up; the title line
   says PRIVILEGED for the whole session, and the log records it. */
#include <stdio.h>
#include <string.h>
#include <ssdef.h>
#include "vt_app.h"
#include "vt_util.h"

int main(int argc, char **argv)
{
    static vt_app app;
    vtp_session sess;
    unsigned int st;
    int rows, cols, ok, i, privileged = 0;
    const char *spec = 0;
    char msg[256];

    for (i = 1; i < argc; i++) {           /* the C RTL lowercases them: compare blind */
        if (argv[i][0] == '/') {
            if (vt_strncasecmp(argv[i], "/PRIVILEGES", strlen(argv[i])) == 0 && strlen(argv[i]) >= 5)
                privileged = 1;
            else if (vt_strncasecmp(argv[i], "/NOPRIVILEGES", strlen(argv[i])) == 0 && strlen(argv[i]) >= 7)
                privileged = 0;
            else { fprintf(stderr, "VTREE: unknown qualifier %s (only /PRIVILEGES)\n", argv[i]); return SS$_BADPARAM; }
        } else if (!spec) spec = argv[i];
        else { fprintf(stderr, "VTREE: one start directory only\n"); return SS$_BADPARAM; }
    }
    st = vtp_start(&sess, privileged);
    if (!(st & 1)) {
        vtp_message(st, msg, sizeof msg);
        fprintf(stderr, "VTREE: cannot start: %s\n", msg);
        return st;
    }
    vtp_screen_size(&rows, &cols);
    if (rows < 16 || cols < 60) {
        vtp_stop();
        fprintf(stderr, "VTREE: needs at least 16 rows and 60 columns (this terminal: %d x %d)\n",
                rows, cols);
        return SS$_ABORT;
    }
    ok = vt_app_init(&app, spec, rows, cols);
    app.sess = sess;
    if (!ok) {
        vtp_stop();
        fprintf(stderr, "VTREE: %s\n", app.msg[0] ? app.msg : "cannot read the start directory");
        return SS$_ABORT;
    }
    vt_app_session(&app, privileged);
    vt_app_run(&app);
    vt_app_free(&app);
    vtp_stop();
    return SS$_NORMAL;
}
