#include "providers/usage.h"

#include <math.h>

#define PERCENT_MAX 100.0

lfcc_status_t usage_fraction_from_percent(double percent, double *out_fraction)
{
    double clamped = percent;

    if (out_fraction == NULL) {
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
    *out_fraction = clamped / PERCENT_MAX;
    return LFCC_OK;
}
