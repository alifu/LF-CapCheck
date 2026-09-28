#include "ui/watch.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "ui/usage_view.h"

#define CHART_TEXT_MAX 2048
#define COMMAND_MAX_CHARS 64
#define MAX_INTERVAL_DIGITS 4 /* "3600" */
#define CLEAR_SCREEN "\x1b[H\x1b[2J"

typedef enum { WAIT_TIMEOUT, WAIT_REFRESH, WAIT_QUIT, WAIT_END } wait_result_t;

/* ---- arguments ---- */

static lfcc_status_t reject(char *error, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static lfcc_status_t reject(char *error, size_t cap, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(error, cap, fmt, args);
    va_end(args);
    return LFCC_ERR_INVALID_ARG;
}

/* A plain whole number 1..WATCH_MAX_INTERVAL_SECONDS: digits only, no sign, no spaces. */
static bool parse_interval(const char *text, unsigned *seconds)
{
    unsigned value = 0;
    size_t length = strlen(text);

    if (length == 0 || length > MAX_INTERVAL_DIGITS) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (!isdigit((unsigned char)text[i])) {
            return false;
        }
        value = value * 10 + (unsigned)(text[i] - '0');
    }
    if (value < 1 || value > WATCH_MAX_INTERVAL_SECONDS) {
        return false;
    }
    *seconds = value;
    return true;
}

lfcc_status_t watch_parse_args(int argc, const char *const argv[], watch_options_t *out,
                               char *error, size_t error_cap)
{
    watch_options_t options = {WATCH_DEFAULT_INTERVAL_SECONDS, NULL, false, 0};
    bool interval_given = false;

    if (out == NULL || error == NULL || error_cap == 0 || (argc > 0 && argv == NULL)) {
        return LFCC_ERR_INVALID_ARG;
    }
    error[0] = '\0';

    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--interval") == 0) {
            if (interval_given) {
                return reject(error, error_cap, "--interval given more than once");
            }
            if (i + 1 >= argc || !parse_interval(argv[i + 1], &options.interval_seconds)) {
                return reject(error, error_cap,
                              "--interval needs a whole number of seconds from 1 to %d",
                              WATCH_MAX_INTERVAL_SECONDS);
            }
            interval_given = true;
            i++;
        } else if (arg[0] == '-') {
            return reject(error, error_cap, "unknown option '%s'", arg);
        } else if (options.provider_id != NULL) {
            return reject(error, error_cap, "give only one provider name, not '%s' as well", arg);
        } else {
            options.provider_id = arg;
        }
    }
    *out = options;
    return LFCC_OK;
}

/* ---- waiting for the timer or a typed line ---- */

/* Reads one line; q (any case) as its first non-blank character quits, anything else refreshes. */
static wait_result_t read_command(int fd)
{
    char first = '\0';
    char character = '\0';
    size_t read_count = 0;
    ssize_t got = 0;

    while ((got = read(fd, &character, 1)) > 0 && character != '\n') {
        if (first == '\0' && !isspace((unsigned char)character)) {
            first = character;
        }
        read_count++;
    }
    if (got <= 0 && read_count == 0) {
        return WAIT_END; /* end of input with nothing typed */
    }
    return tolower((unsigned char)first) == 'q' ? WAIT_QUIT : WAIT_REFRESH;
}

static wait_result_t wait_for_command(int fd, unsigned seconds)
{
    fd_set readable;
    struct timeval timeout = {(time_t)seconds, 0};
    int ready = 0;

    if (fd < 0 || fd >= FD_SETSIZE) {
        return WAIT_END;
    }
    memset(&readable, 0, sizeof readable); /* what FD_ZERO does, without its bzero() macro */
    FD_SET(fd, &readable);
    ready = select(fd + 1, &readable, NULL, NULL, &timeout);
    if (ready == 0 || (ready < 0 && errno == EINTR)) {
        return WAIT_TIMEOUT;
    }
    return ready < 0 ? WAIT_END : read_command(fd);
}

/* ---- one frame ---- */

static time_t current_time(const menu_env_t *env)
{
    return env->now != 0 ? env->now : time(NULL);
}

static void print_footer(const menu_env_t *env, unsigned interval)
{
    fprintf(env->out,
            "\nUpdating every %u s. Press Enter to refresh now, or type q and press Enter to quit.\n",
            interval);
}

/* Draws the provider's current state. Returns the status the provider reported. */
static lfcc_status_t draw_frame(const provider_t *provider, const menu_env_t *env,
                                const watch_options_t *options)
{
    provider_env_t provider_env = {current_time(env), env->data_dir};
    usage_snapshot_t usage = {0};
    char chart[CHART_TEXT_MAX];
    lfcc_status_t status = provider->load_usage(provider, &provider_env, &usage);

    if (options->clear_screen) {
        fputs(CLEAR_SCREEN, env->out);
    }
    if (status == LFCC_OK) {
        status = usage_view_render(provider, &usage, &env->style, provider_env.now, chart, sizeof chart);
        fprintf(env->out, "\n%s", status == LFCC_OK ? chart : "Cannot draw the chart.\n");
        status = LFCC_OK;
    } else if (status == LFCC_ERR_NOT_CONNECTED) {
        fprintf(env->out, "\n%s is not connected yet. Run lf-capcheck and choose %s for the setup steps.\n",
                provider->display_name, provider->display_name);
    } else if (status == LFCC_ERR_UNAVAILABLE) {
        fprintf(env->out, "\n%s is not available yet.\n", provider->display_name);
        return status;
    } else {
        fprintf(env->out, "\nCannot show %s: %s.\n", provider->display_name, lfcc_status_str(status));
    }
    print_footer(env, options->interval_seconds);
    fflush(env->out);
    return status == LFCC_ERR_NOT_CONNECTED ? LFCC_OK : status;
}

int watch_run(const provider_t *provider, const menu_env_t *env, const watch_options_t *options)
{
    unsigned frames = 0;

    if (provider == NULL || env == NULL || options == NULL || env->in == NULL ||
        env->out == NULL || env->data_dir == NULL || options->interval_seconds < 1 ||
        options->interval_seconds > WATCH_MAX_INTERVAL_SECONDS) {
        return MENU_EXIT_ERROR;
    }

    for (;;) {
        wait_result_t next = WAIT_TIMEOUT;

        if (draw_frame(provider, env, options) == LFCC_ERR_UNAVAILABLE) {
            return MENU_EXIT_ERROR;
        }
        frames++;
        if (options->max_frames != 0 && frames >= options->max_frames) {
            return MENU_EXIT_OK;
        }
        next = wait_for_command(fileno(env->in), options->interval_seconds);
        if (next == WAIT_QUIT || next == WAIT_END) {
            return MENU_EXIT_OK;
        }
    }
}
