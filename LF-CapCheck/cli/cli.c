#include "cli/cli.h"

#include <limits.h>
#include <string.h>

#include "cli/statusline.h"
#include "providers/builtin.h"
#include "ui/executable_path.h"
#include "ui/menu.h"
#include "util/log.h"
#include "util/secure_path.h"
#include "util/version.h"

static void print_usage(FILE *out)
{
    fprintf(out,
            "Usage: " LFCC_NAME " [command | option]\n"
            "\n"
            "Shows the remaining usage limits of your AI subscriptions.\n"
            "Run without arguments to open the interactive menu.\n"
            "\n"
            "Commands:\n"
            "  statusline     called by Claude Code: reads its status line JSON on stdin,\n"
            "                 saves the usage limits and prints a one-line summary\n"
            "\n"
            "Options:\n"
            "  -h, --help     show this help\n"
            "      --version  show the version\n");
}

/* Opens the interactive menu with the built-in providers. */
static int run_menu(const cli_io_t *io)
{
    char data_dir[PATH_MAX];
    char running_path[PATH_MAX];
    char command_path[PATH_MAX];
    registry_t registry;
    menu_env_t env = {io->in, io->out, io->data_dir, io->executable, io->now, io->style};
    lfcc_status_t status = builtin_registry_build(&registry);

    if (status == LFCC_OK && env.data_dir == NULL) {
        status = secure_default_data_dir(data_dir, sizeof data_dir);
        env.data_dir = data_dir;
    }
    if (status == LFCC_OK && env.executable == NULL) {
        status = executable_current_path(running_path, sizeof running_path);
        if (status == LFCC_OK) {
            status = executable_stable_path(running_path, command_path, sizeof command_path);
        }
        env.executable = command_path;
    }
    if (status != LFCC_OK) {
        log_msg(LFCC_LOG_ERROR, "cannot start: %s", lfcc_status_str(status));
        return CLI_EXIT_ERROR;
    }
    return menu_run(&registry, &env) == MENU_EXIT_OK ? CLI_EXIT_OK : CLI_EXIT_ERROR;
}

static int dispatch(int argc, const char *const argv[], const cli_io_t *io)
{
    const char *arg = NULL;

    if (argc <= 1) {
        return run_menu(io);
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
