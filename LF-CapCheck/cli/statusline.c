#include "cli/statusline.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "providers/usage.h"
#include "store/snapshot_merge.h"
#include "store/snapshot_store.h"
#include "util/log.h"
#include "util/percent.h"
#include "util/secure_path.h"
#include "util/text_append.h"
#include "util/version.h"

#define DEFAULT_TIMEOUT_SECONDS 5 /* normal runs take ~2 ms */
#define SUMMARY_MAX_CHARS 128
#define PATH_MAX_CHARS 1024
#define SEPARATOR " \xc2\xb7 " /* " · " */

/* ---- one-line summary ---- */

static bool window_is_showable(const snap_window_t *window, time_t now)
{
    return window->present && (window->resets_at == 0 || window->resets_at > now);
}

static bool append_window(char *out, size_t cap, size_t *used, const char *label,
                          const snap_window_t *window, time_t now)
{
    if (!window_is_showable(window, now)) {
        return true;
    }
    /* Same conversion and rounding as the Claude provider and the chart. */
    return text_append(out, cap, used, "%s%s %d%% left", *used > 0 ? SEPARATOR : "", label,
                       usage_percent_left(window->used_percentage / PERCENT_MAX));
}

lfcc_status_t statusline_format(const snap_record_t *record, time_t now, char *out, size_t cap)
{
    size_t used = 0;

    if (record == NULL || out == NULL || cap == 0) {
        return LFCC_ERR_INVALID_ARG;
    }
    out[0] = '\0';

    if (!append_window(out, cap, &used, "5h", &record->five_hour, now) ||
        !append_window(out, cap, &used, "7d", &record->seven_day, now)) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    return LFCC_OK;
}

/* ---- reading Claude Code's payload ---- */

/* Reads everything on `in`, up to CLAUDE_INPUT_MAX_BYTES. Returns false if it was larger. */
static bool read_payload(FILE *in, char *buffer, size_t *length)
{
    *length = fread(buffer, 1, CLAUDE_INPUT_MAX_BYTES + 1, in);
    return *length <= CLAUDE_INPUT_MAX_BYTES;
}

static bool load_incoming(FILE *in, time_t now, snap_record_t *incoming)
{
    char *payload = malloc(CLAUDE_INPUT_MAX_BYTES + 1);
    size_t length = 0;
    bool usable = false;

    if (payload == NULL) {
        return false;
    }
    if (read_payload(in, payload, &length)) {
        usable = claude_input_parse(payload, length, now, incoming) == LFCC_OK;
    }
    free(payload);
    return usable;
}

/* ---- keeping the snapshot ---- */

static void warn_storage(const cli_io_t *io, const char *what, lfcc_status_t status)
{
    log_write(io->err, LFCC_LOG_WARN, "%s: %s", what, lfcc_status_str(status));
}

static bool resolve_data_dir(const cli_io_t *io, char *buffer, size_t cap, const char **dir)
{
    lfcc_status_t status = LFCC_OK;

    if (io->data_dir != NULL) {
        *dir = io->data_dir;
        return true;
    }
    status = secure_default_data_dir(buffer, cap);
    if (status != LFCC_OK) {
        warn_storage(io, "cannot locate the data directory", status);
        return false;
    }
    *dir = buffer;
    return true;
}

/*
 * Merges `incoming` with the previously stored record (when there is a usable
 * one) and returns what should be saved and shown.
 *
 * Known limitation: this read-merge-write is not locked, so two Claude Code
 * sessions updating at the very same instant can lose one update. Nothing is
 * corrupted (the write is atomic) and the next update repairs it.
 */
static snap_record_t merge_with_previous(const cli_io_t *io, int dir_fd,
                                         const snap_record_t *incoming, time_t now)
{
    snap_record_t previous = {0};
    snap_record_t merged = *incoming;
    lfcc_status_t status = snap_read(dir_fd, &previous);

    if (status == LFCC_ERR_NOT_FOUND) {
        return merged; /* first update: nothing to merge */
    }
    if (status != LFCC_OK) {
        /* Unreadable, corrupt or untrusted (e.g. loose permissions): start fresh and say so. */
        warn_storage(io, "ignoring the previous snapshot", status);
        return merged;
    }
    if (snap_merge(incoming, &previous, now, &merged) != LFCC_OK) {
        return *incoming; /* cannot happen with valid arguments; never show half-merged data */
    }
    return merged;
}

/*
 * Saves the merged record. Returns what should be shown: the merged record, or
 * the incoming one if storage is unavailable.
 */
static snap_record_t update_snapshot(const cli_io_t *io, const snap_record_t *incoming, time_t now)
{
    char default_dir[PATH_MAX_CHARS];
    const char *dir = NULL;
    snap_record_t merged = *incoming;
    int fd = -1;
    lfcc_status_t status = LFCC_OK;

    if (!resolve_data_dir(io, default_dir, sizeof default_dir, &dir)) {
        return merged;
    }
    status = secure_dir_open(dir, &fd);
    if (status != LFCC_OK) {
        warn_storage(io, "cannot use the data directory", status);
        return merged;
    }

    merged = merge_with_previous(io, fd, incoming, now);
    status = snap_write(fd, &merged);
    if (status != LFCC_OK) {
        warn_storage(io, "cannot save the usage snapshot", status);
    }
    close(fd);
    return merged;
}

/* ---- never disturb Claude Code: no hanging, no dying from a closed pipe ---- */

static void give_up(int signal_number)
{
    (void)signal_number;
    _exit(CLI_EXIT_OK); /* async-signal-safe; a quiet, successful exit */
}

typedef struct {
    struct sigaction alarm_before;
    struct sigaction pipe_before;
} guard_t;

/*
 * Starts a watchdog (input that never ends must not hang Claude Code's status
 * line) and ignores SIGPIPE (a closed output pipe must not kill us). The
 * previous dispositions are saved so guard_stop() can put them back.
 */
static void guard_start(guard_t *guard, unsigned seconds)
{
    struct sigaction on_alarm;
    struct sigaction ignore;

    memset(&on_alarm, 0, sizeof on_alarm);
    memset(&ignore, 0, sizeof ignore);
    on_alarm.sa_handler = give_up;
    sigemptyset(&on_alarm.sa_mask);
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);

    sigaction(SIGALRM, &on_alarm, &guard->alarm_before);
    sigaction(SIGPIPE, &ignore, &guard->pipe_before);
    alarm(seconds);
}

static void guard_stop(const guard_t *guard)
{
    alarm(0);
    sigaction(SIGALRM, &guard->alarm_before, NULL);
    sigaction(SIGPIPE, &guard->pipe_before, NULL);
}

/* ---- the command ---- */

static void process_payload(const cli_io_t *io, time_t now)
{
    char line[SUMMARY_MAX_CHARS];
    snap_record_t incoming = {0};
    snap_record_t shown = {0};

    if (!load_incoming(io->in, now, &incoming)) {
        return; /* nothing usable: stay silent */
    }
    shown = update_snapshot(io, &incoming, now);
    if (statusline_format(&shown, now, line, sizeof line) == LFCC_OK && line[0] != '\0') {
        fprintf(io->out, "%s\n", line);
    }
}

int statusline_run(const cli_io_t *io)
{
    guard_t guard;
    time_t now = io->now != 0 ? io->now : time(NULL);

    if (isatty(fileno(io->in))) {
        fprintf(io->err,
                "%s: 'statusline' is run by Claude Code and reads its JSON on stdin.\n"
                "Try '%s --help'.\n",
                LFCC_NAME, LFCC_NAME);
        return CLI_EXIT_USAGE;
    }

    guard_start(&guard, io->timeout_seconds != 0 ? io->timeout_seconds : DEFAULT_TIMEOUT_SECONDS);
    process_payload(io, now);
    fflush(io->out); /* flush while SIGPIPE is still ignored */
    guard_stop(&guard);
    return CLI_EXIT_OK;
}
