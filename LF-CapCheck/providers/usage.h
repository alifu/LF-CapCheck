#ifndef LFCC_USAGE_H
#define LFCC_USAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "util/status.h"

#define USAGE_MAX_WINDOWS 4
#define USAGE_LABEL_MAX 48

/* One limit window, e.g. "5-hour session". Built once, then passed around as const. */
typedef struct {
    char label[USAGE_LABEL_MAX];
    double used_fraction; /* 0.0 (nothing used) .. 1.0 (limit reached) */
    time_t resets_at;     /* 0 when unknown */
    bool expired;         /* resets_at has passed; the fraction is stale */
} usage_window_t;

/* Everything the chart needs for one provider. */
typedef struct {
    const char *provider_id; /* points at the provider's static id */
    usage_window_t windows[USAGE_MAX_WINDOWS];
    size_t window_count;
    time_t as_of; /* when the data was captured */
} usage_snapshot_t;

/*
 * Whole percent still available for a window that is `used_fraction` used
 * (0.0..1.0), rounded half up. Out-of-range values are clamped; NaN and
 * infinity count as used up. Every place that shows a percentage must use this,
 * so the chart and the status line can never disagree.
 */
int usage_percent_left(double used_fraction);

/* True when the snapshot has at least one window and every window has been reset. */
bool usage_all_expired(const usage_snapshot_t *snapshot);

/*
 * Clamps a percentage from external data into 0..100. NaN and infinity are
 * rejected with LFCC_ERR_PARSE and *out_percent is left untouched.
 */
lfcc_status_t usage_clamp_percent(double percent, double *out_percent);

/*
 * Converts a 0..100 percentage from external data into a 0.0..1.0 fraction,
 * clamping out-of-range values. NaN and infinity are rejected with
 * LFCC_ERR_PARSE and *out_fraction is left untouched.
 */
lfcc_status_t usage_fraction_from_percent(double percent, double *out_fraction);

#endif /* LFCC_USAGE_H */
