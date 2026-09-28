#ifndef LFCC_CHART_H
#define LFCC_CHART_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "providers/usage.h"
#include "util/status.h"

typedef struct {
    int width;         /* terminal columns; <= 0 means 80 */
    bool color;        /* ANSI colours on the bars */
    bool unicode;      /* block characters; otherwise '#' and '-' */
    time_t now;        /* current time, epoch seconds */
    const char *title; /* provider display name, e.g. "Claude" */
} chart_options_t;

/*
 * Draws the remaining-usage bar chart as text (lines end with '\n').
 *
 *   Claude - remaining usage                 as of 11:57 (3 min ago)
 *   5-hour session ████████████░░░░░░░░  62% left   resets in 2h 14m
 *   Weekly         ████░░░░░░░░░░░░░░░░  21% left   resets in 3d 4h
 *
 * The bar shows what is LEFT. Colour (when enabled) is green above 50% left,
 * yellow from 20% to 50%, red below 20%; the percentage is always printed so
 * colour is never the only signal. A window whose reset time has passed shows
 * "reset - waiting for new data" instead of a bar.
 *
 * Pure: writes only into `out`. LFCC_ERR_INVALID_ARG for NULL arguments, no
 * room for a title, or more than USAGE_MAX_WINDOWS windows; LFCC_ERR_CAPACITY
 * when `cap` is too small (out becomes "").
 */
lfcc_status_t chart_render(const usage_snapshot_t *snapshot, const chart_options_t *options,
                           char *out, size_t cap);

#endif /* LFCC_CHART_H */
