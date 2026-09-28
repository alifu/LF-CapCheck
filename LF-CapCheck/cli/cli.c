#include "cli/cli.h"

#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "cli/statusline.h"
#include "providers/builtin.h"
#include "ui/executable_path.h"
#include "ui/menu.h"
#include "ui/watch.h"
#include "util/log.h"
#include "util/secure_path.h"
#include "util/text_append.h"
#include "util/version.h"

#define WATCH_ERROR_MAX 160
#define PROVIDER_LIST_MAX 128

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
            "  -w, --watch [--interval SECONDS] [PROVIDER]\n"
            "                 keep one provider's chart up to date (default: the first provider,\n"
            "                 every 5 s); press Enter to refresh, type q and Enter to quit\n"
            "  -h, --help     show this help\n"
            "      --version  show the version\n");
}

/* ---- what the menu and the watch mode both need ---- */

typedef struct {
    registry_t registry;
    menu_env_t env;
    char data_dir[PATH_MAX];
    char running_path[PATH_MAX];
    char command_path[PATH_MAX];
} session_t;

/* Builds the provider registry and fills in the data directory and program path if not injected. */
static lfcc_status_t session_open(const cli_io_t *io, session_t *session)
{
    lfcc_status_t status = builtin_registry_build(&session->registry);

    session->env = (menu_env_t){io->in, io->out, io->data_dir, io->executable, io->now, io->style};

    if (status == LFCC_OK && session->env.data_dir == NULL) {
        status = secure_default_data_dir(session->data_dir, sizeof session->data_dir);
        session->env.data_dir = session->data_dir;
    }
    if (status == LFCC_OK && session->env.executable == NULL) {
        status = executable_current_path(session->running_path, sizeof session->running_path);
        if (status == LFCC_OK) {
            status = executable_stable_path(session->running_path, session->command_path,
                                            sizeof session->command_path);
        }
        session->env.executable = session->command_path;
    }
    return status;
}

static int run_menu(const cli_io_t *io)
{
    session_t session;
    lfcc_status_t status = session_open(io, &session);

    if (status != LFCC_OK) {
        log_write(io->err, LFCC_LOG_ERROR, "cannot start: %s", lfcc_status_str(status));
        return CLI_EXIT_ERROR;
    }
    return menu_run(&session.registry, &session.env) == MENU_EXIT_OK ? CLI_EXIT_OK : CLI_EXIT_ERROR;
}

/* "claude, codex": the ids a user may pass to --watch. */
static void list_provider_ids(const registry_t *registry, char *out, size_t cap)
{
    size_t used = 0;

    out[0] = '\0';
    for (size_t i = 0; i < registry_count(registry); i++) {
        if (!text_append(out, cap, &used, "%s%s", i > 0 ? ", " : "", registry_at(registry, i)->id)) {
            return;
        }
    }
}

static int run_watch(int argc, const char *const argv[], const cli_io_t *io)
{
    watch_options_t options;
    char error[WATCH_ERROR_MAX];
    char known[PROVIDER_LIST_MAX];
    session_t session;
    const provider_t *provider = NULL;
    lfcc_status_t status = watch_parse_args(argc - 2, argv + 2, &options, error, sizeof error);

    if (status != LFCC_OK) {
        log_write(io->err, LFCC_LOG_ERROR, "%s", error); /* sanitised: it quotes untrusted arguments */
        fprintf(io->err, "Try '%s --help'.\n", LFCC_NAME);
        return CLI_EXIT_USAGE;
    }
    status = session_open(io, &session);
    if (status != LFCC_OK) {
        log_write(io->err, LFCC_LOG_ERROR, "cannot start: %s", lfcc_status_str(status));
        return CLI_EXIT_ERROR;
    }

    provider = options.provider_id == NULL ? registry_at(&session.registry, 0)
                                           : registry_find(&session.registry, options.provider_id);
    if (provider == NULL) {
        list_provider_ids(&session.registry, known, sizeof known);
        log_write(io->err, LFCC_LOG_ERROR, "unknown provider '%s' (available: %s)",
                  options.provider_id, known);
        return CLI_EXIT_USAGE;
    }

    /* Redraw in place only on a real, capable terminal; anywhere else frames are appended. */
    options.clear_screen = io->style.unicode && isatty(fileno(io->out));
    return watch_run(provider, &session.env, &options) == MENU_EXIT_OK ? CLI_EXIT_OK : CLI_EXIT_ERROR;
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
    if (strcmp(arg, "--watch") == 0 || strcmp(arg, "-w") == 0) {
        return run_watch(argc, argv, io);
    }
    if (strcmp(arg, "--version") == 0) {
        fprintf(io->out, "%s %s\n", LFCC_NAME, LFCC_VERSION);
        return CLI_EXIT_OK;
    }
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        print_usage(io->out);
        return CLI_EXIT_OK;
    }

    log_write(io->err, LFCC_LOG_ERROR, "unknown option '%s'", arg); /* sanitised: arg is untrusted */
    fprintf(io->err, "Try '%s --help'.\n", LFCC_NAME);
    return CLI_EXIT_USAGE;
}

int cli_run(int argc, const char *const argv[], const cli_io_t *io)
{
    return dispatch(argc, argv, io);
}
