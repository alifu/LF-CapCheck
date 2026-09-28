#ifndef LFCC_CLI_H
#define LFCC_CLI_H

#include <stdio.h>

#define CLI_EXIT_OK 0
#define CLI_EXIT_USAGE 2

/*
 * Runs the command line and returns the process exit code.
 * Normal output goes to `out`, diagnostics to `err`; never calls exit().
 */
int cli_run(int argc, const char *const argv[], FILE *out, FILE *err);

#endif /* LFCC_CLI_H */
