#ifndef LFCC_STATUSLINE_H
#define LFCC_STATUSLINE_H

#include <stddef.h>
#include <time.h>

#include "cli/cli.h"
#include "providers/claude_input.h"
#include "store/snapshot_codec.h"
#include "util/status.h"

/*
 * One-line summary for Claude Code to display, e.g.
 * "5h 77% left · 7d 59% left". Windows that are absent or already reset are
 * left out; the result is "" when nothing can be shown.
 * LFCC_ERR_CAPACITY if `cap` is too small (out becomes "").
 */
lfcc_status_t statusline_format(const snap_record_t *record, time_t now, char *out, size_t cap);

/*
 * The `statusline` command, run by Claude Code on every status refresh.
 * Reads the payload from io->in, keeps the rate limits in the private
 * snapshot file and prints the one-line summary to io->out.
 *
 * It must never disturb Claude Code, so once input is being piped in it always
 * returns 0 and stays silent on stdout for input it cannot use. Storage
 * problems are reported as warnings on io->err and never stop the summary.
 * Run from a terminal (no piped input) it explains itself and returns
 * CLI_EXIT_USAGE instead of waiting for input.
 */
int statusline_run(const cli_io_t *io);

#endif /* LFCC_STATUSLINE_H */
