#include "cli/statusline.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "store/snapshot_merge.h"
#include "store/snapshot_store.h"
#include "util/log.h"
#include "util/secure_path.h"
#include "util/text_append.h"
#include "util/version.h"

#define LINE_MAX_CHARS 128
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
    int remaining_percent = 0;

    if (!window_is_showable(window, now)) {
        return true;
    }
    /* used_percentage is validated to 0..100, so this stays in 0..100. */
    remaining_percent = (int)(100.0 - window->used_percentage + 0.5);
    return text_append(out, cap, used, "%s%s %d%% left", *used > 0 ? SEPARATOR : "", label,
                       remaining_percent);
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

static void warn_storage(const char *what, lfcc_status_t status)
{
    log_msg(LFCC_LOG_WARN, "%s: %s", what, lfcc_status_str(status));
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
        warn_storage("cannot locate the data directory", status);
        return false;
    }
    *dir = buffer;
    return true;
}

/*
 * Merges with the stored record and saves the result. Returns what should be
 * shown: the merged record, or the incoming one if storage is unavailable.
 */
static snap_record_t update_snapshot(const cli_io_t *io, const snap_record_t *incoming, time_t now)
{
    char default_dir[PATH_MAX_CHARS];
    const char *dir = NULL;
    snap_record_t previous = {0};
    snap_record_t merged = *incoming;
    int fd = -1;
    lfcc_status_t status = LFCC_OK;

    if (!resolve_data_dir(io, default_dir, sizeof default_dir, &dir)) {
        return merged;
    }
    status = secure_dir_open(dir, &fd);
    if (status != LFCC_OK) {
        warn_storage("cannot use the data directory", status);
        return merged;
    }

    /* A missing or unreadable previous snapshot is not an error: start fresh. */
    if (snap_read(fd, &previous) == LFCC_OK) {
        snap_merge(incoming, &previous, now, &merged);
    }
    status = snap_write(fd, &merged);
    if (status != LFCC_OK) {
        warn_storage("cannot save the usage snapshot", status);
    }
    close(fd);
    return merged;
}

/* ---- the command ---- */

int statusline_run(const cli_io_t *io)
{
    char line[LINE_MAX_CHARS];
    snap_record_t incoming = {0};
    snap_record_t shown = {0};
    time_t now = io->now != 0 ? io->now : time(NULL);

    if (isatty(fileno(io->in))) {
        fprintf(io->err,
                "%s: 'statusline' is run by Claude Code and reads its JSON on stdin.\n"
                "Try '%s --help'.\n",
                LFCC_NAME, LFCC_NAME);
        return CLI_EXIT_USAGE;
    }
    if (!load_incoming(io->in, now, &incoming)) {
        return CLI_EXIT_OK; /* nothing usable: stay silent, never disturb Claude Code */
    }

    shown = update_snapshot(io, &incoming, now);
    if (statusline_format(&shown, now, line, sizeof line) == LFCC_OK && line[0] != '\0') {
        fprintf(io->out, "%s\n", line);
    }
    return CLI_EXIT_OK;
}
