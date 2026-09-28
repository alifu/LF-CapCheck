#include "cli/cli.h"

#include <string.h>

#include "util/version.h"

static void print_usage(FILE *out)
{
    fprintf(out,
            "Usage: " LFCC_NAME " [--help] [--version]\n"
            "\n"
            "Shows the remaining usage limits of your AI subscriptions.\n"
            "\n"
            "Options:\n"
            "  -h, --help     show this help\n"
            "      --version  show the version\n");
}

int cli_run(int argc, const char *const argv[], FILE *out, FILE *err)
{
    if (argc <= 1) {
        print_usage(out);
        return CLI_EXIT_OK;
    }

    const char *arg = argv[1];

    if (strcmp(arg, "--version") == 0) {
        fprintf(out, "%s %s\n", LFCC_NAME, LFCC_VERSION);
        return CLI_EXIT_OK;
    }
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        print_usage(out);
        return CLI_EXIT_OK;
    }

    fprintf(err, "%s: unknown option '%s'\nTry '%s --help'.\n", LFCC_NAME, arg,
            LFCC_NAME);
    return CLI_EXIT_USAGE;
}
