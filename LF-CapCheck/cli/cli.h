#ifndef LFCC_CLI_H
#define LFCC_CLI_H

#include <stdio.h>
#include <time.h>

#include "ui/terminal.h"

#define CLI_EXIT_OK 0
#define CLI_EXIT_ERROR 1
#define CLI_EXIT_USAGE 2

/* Everything the commands touch from the outside world, so tests can inject it. */
typedef struct {
    FILE *in;
    FILE *out;
    FILE *err;
    const char *data_dir;   /* NULL: the per-user default directory */
    time_t now;             /* 0: the current time */
    const char *executable; /* NULL: this program's own path (for the connect snippet) */
    terminal_style_t style; /* how the menu draws */
    unsigned timeout_seconds; /* `statusline` watchdog; 0: the default (5 s) */
} cli_io_t;

/*
 * Runs the command line and returns the process exit code.
 * Normal output goes to io->out, diagnostics to io->err; never calls exit().
 */
int cli_run(int argc, const char *const argv[], const cli_io_t *io);

#endif /* LFCC_CLI_H */
