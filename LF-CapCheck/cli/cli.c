#include "cli/cli.h"

#include <string.h>

#include "cli/statusline.h"
#include "util/log.h"
#include "util/version.h"

static void print_usage(FILE *out)
{
    fprintf(out,
            "Usage: " LFCC_NAME " [command | option]\n"
            "\n"
            "Shows the remaining usage limits of your AI subscriptions.\n"
            "\n"
            "Commands:\n"
            "  statusline     called by Claude Code: reads its status line JSON on stdin,\n"
            "                 saves the usage limits and prints a one-line summary\n"
            "\n"
            "Options:\n"
            "  -h, --help     show this help\n"
            "      --version  show the version\n");
}

static int dispatch(int argc, const char *const argv[], const cli_io_t *io)
{
    const char *arg = NULL;

    if (argc <= 1) {
        print_usage(io->out);
        return CLI_EXIT_OK;
    }

    arg = argv[1];
    if (strcmp(arg, "statusline") == 0) {
        return statusline_run(io);
    }
    if (strcmp(arg, "--version") == 0) {
        fprintf(io->out, "%s %s\n", LFCC_NAME, LFCC_VERSION);
        return CLI_EXIT_OK;
    }
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        print_usage(io->out);
        return CLI_EXIT_OK;
    }

    fprintf(io->err, "%s: unknown option '%s'\nTry '%s --help'.\n", LFCC_NAME, arg, LFCC_NAME);
    return CLI_EXIT_USAGE;
}

int cli_run(int argc, const char *const argv[], const cli_io_t *io)
{
    int exit_code = 0;

    log_set_stream(io->err);
    exit_code = dispatch(argc, argv, io);
    log_set_stream(NULL);
    return exit_code;
}
