#include "store/snapshot_merge.h"

/* Picks the incoming window, else the previous one while it is still valid (*carried = true). */
static snap_window_t choose_window(const snap_window_t *incoming, const snap_window_t *previous,
                                   time_t now, bool *carried)
{
    snap_window_t none = {false, 0.0, 0};

    if (incoming->present) {
        return *incoming;
    }
    if (previous != NULL && previous->present && previous->resets_at > now) {
        *carried = true;
        return *previous;
    }
    return none;
}

lfcc_status_t snap_merge(const snap_record_t *incoming, const snap_record_t *previous, time_t now,
                         snap_record_t *out)
{
    snap_record_t merged = {0};
    bool carried = false;

    if (incoming == NULL || out == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    merged.five_hour = choose_window(&incoming->five_hour,
                                     previous != NULL ? &previous->five_hour : NULL, now, &carried);
    merged.seven_day = choose_window(&incoming->seven_day,
                                     previous != NULL ? &previous->seven_day : NULL, now, &carried);

    /* A carried window is older than the incoming data, so the record must not look fresher. */
    merged.as_of = incoming->as_of;
    if (carried && previous->as_of < merged.as_of) {
        merged.as_of = previous->as_of;
    }
    *out = merged;
    return LFCC_OK;
}
