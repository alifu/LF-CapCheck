#ifndef LFCC_WATCH_H
#define LFCC_WATCH_H

#include <stdbool.h>
#include <stddef.h>

#include "providers/provider.h"
#include "ui/menu.h"
#include "util/status.h"

#define WATCH_DEFAULT_INTERVAL_SECONDS 5
#define WATCH_MAX_INTERVAL_SECONDS 3600

typedef struct {
    unsigned interval_seconds; /* 1..WATCH_MAX_INTERVAL_SECONDS */
    const char *provider_id;   /* from the command line; NULL means the first provider */
    bool clear_screen;         /* redraw in place (interactive terminal) instead of appending */
    unsigned max_frames;       /* stop after this many frames; 0 means until the user quits */
} watch_options_t;

/*
 * Parses what follows `--watch` on the command line:
 *
 *   [--interval SECONDS] [PROVIDER]
 *
 * SECONDS is a plain whole number from 1 to WATCH_MAX_INTERVAL_SECONDS; at most
 * one PROVIDER id may be given; the options may come in either order.
 * `*out` is fully initialised (interval 5 s, no provider, no clearing, no frame
 * limit) before the arguments are applied. `argv` may be NULL when `argc` is 0.
 *
 * LFCC_ERR_INVALID_ARG with a one-line explanation in `error` (safe to print,
 * arguments are quoted into it and must be sanitised by the printer) for
 * anything wrong, including NULL `out`/`error`.
 */
lfcc_status_t watch_parse_args(int argc, const char *const argv[], watch_options_t *out,
                               char *error, size_t error_cap);

/*
 * Keeps one provider's chart on screen and reloads it every interval:
 *
 *   - Enter refreshes at once; a line starting with q (any case) quits;
 *     end of input quits too, so `lf-capcheck --watch < /dev/null` prints once.
 *   - Data that arrives while watching is picked up on the next frame.
 *   - Not connected, or unreadable data, is shown and watching continues.
 *   - A provider that is not available yet is reported and returns at once.
 *
 * Input is read line by line straight from the descriptor, so a timer and a
 * typed line can both wake the loop, and the terminal mode is never changed.
 * Returns MENU_EXIT_OK, or MENU_EXIT_ERROR for missing arguments, an
 * out-of-range interval or an unavailable provider.
 */
int watch_run(const provider_t *provider, const menu_env_t *env, const watch_options_t *options);

#endif /* LFCC_WATCH_H */
