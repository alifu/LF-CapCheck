#include "providers/usage.h"

#include <math.h>

#include "util/percent.h"
#include "util/time_math.h"

/* Floating-point error must not turn an exact x.5 tie (e.g. 22.499999999999996) downward. */
#define ROUNDING_TIE_EPSILON 1e-9

lfcc_status_t usage_clamp_percent(double percent, double *out_percent)
{
    double clamped = percent;

    if (out_percent == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    if (!isfinite(percent)) {
        return LFCC_ERR_PARSE;
    }
    if (clamped < 0.0) {
        clamped = 0.0;
    } else if (clamped > PERCENT_MAX) {
        clamped = PERCENT_MAX;
    }
    *out_percent = clamped;
    return LFCC_OK;
}

lfcc_status_t usage_fraction_from_percent(double percent, double *out_fraction)
{
    double clamped = 0.0;
    lfcc_status_t status = LFCC_OK;

    if (out_fraction == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    status = usage_clamp_percent(percent, &clamped);
    if (status != LFCC_OK) {
        return status;
    }
    *out_fraction = clamped / PERCENT_MAX;
    return LFCC_OK;
}

int usage_percent_left(double used_fraction)
{
    double used = isfinite(used_fraction) ? used_fraction : 1.0;

    used = used < 0.0 ? 0.0 : (used > 1.0 ? 1.0 : used);
    return (int)((1.0 - used) * PERCENT_MAX + 0.5 + ROUNDING_TIE_EPSILON);
}

bool usage_is_stale(const usage_snapshot_t *snapshot, time_t now)
{
    return snapshot != NULL && time_seconds_between(snapshot->as_of, now) > USAGE_STALE_AFTER_SECONDS;
}

bool usage_all_expired(const usage_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->window_count == 0) {
        return false;
    }
    for (size_t i = 0; i < snapshot->window_count; i++) {
        if (!snapshot->windows[i].expired) {
            return false;
        }
    }
    return true;
}
