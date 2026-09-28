#include "ui/time_text.h"

#include <stdio.h>

#define SECONDS_PER_MINUTE 60L
#define SECONDS_PER_HOUR 3600L
#define SECONDS_PER_DAY 86400L

lfcc_status_t time_text_age(long seconds, char *out, size_t cap)
{
    int written = 0;

    if (out == NULL || cap == 0) {
        return LFCC_ERR_INVALID_ARG;
    }

    if (seconds < SECONDS_PER_MINUTE) {
        written = snprintf(out, cap, "just now");
    } else if (seconds < SECONDS_PER_HOUR) {
        written = snprintf(out, cap, "%ld min ago", seconds / SECONDS_PER_MINUTE);
    } else if (seconds < SECONDS_PER_DAY) {
        written = snprintf(out, cap, "%ldh %02ldm ago", seconds / SECONDS_PER_HOUR,
                           (seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
    } else {
        written = snprintf(out, cap, "%ldd ago", seconds / SECONDS_PER_DAY);
    }

    if (written < 0 || (size_t)written >= cap) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    return LFCC_OK;
}
