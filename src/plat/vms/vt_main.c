/* vt_main.c - VTREE [start-directory]   (run as a foreign command) */
#include <stdio.h>
#include <string.h>
#include <ssdef.h>
#include "vt_app.h"

int main(int argc, char **argv)
{
    static vt_app app;
    vtp_session sess;
    unsigned int st;
    int rows, cols, ok;
    char msg[256];

    st = vtp_start(&sess);
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
    ok = vt_app_init(&app, argc > 1 ? argv[1] : 0, rows, cols);
    app.sess = sess;
    if (!ok) {
        vtp_stop();
        fprintf(stderr, "VTREE: %s\n", app.msg[0] ? app.msg : "cannot read the start directory");
        return SS$_ABORT;
    }
    vt_app_run(&app);
    vt_app_free(&app);
    vtp_stop();
    return SS$_NORMAL;
}
