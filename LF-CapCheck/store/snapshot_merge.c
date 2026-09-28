#include "store/snapshot_merge.h"

static snap_window_t choose_window(const snap_window_t *incoming, const snap_window_t *previous,
                                   time_t now)
{
    snap_window_t none = {false, 0.0, 0};

    if (incoming->present) {
        return *incoming;
    }
    if (previous != NULL && previous->present && previous->resets_at > now) {
        return *previous;
    }
    return none;
}

lfcc_status_t snap_merge(const snap_record_t *incoming, const snap_record_t *previous, time_t now,
                         snap_record_t *out)
{
    snap_record_t merged = {0};

    if (incoming == NULL || out == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    merged.as_of = incoming->as_of;
    merged.five_hour = choose_window(&incoming->five_hour,
                                     previous != NULL ? &previous->five_hour : NULL, now);
    merged.seven_day = choose_window(&incoming->seven_day,
                                     previous != NULL ? &previous->seven_day : NULL, now);
    *out = merged;
    return LFCC_OK;
}
