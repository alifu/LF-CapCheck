#ifndef LFCC_SNAPSHOT_MERGE_H
#define LFCC_SNAPSHOT_MERGE_H

#include <time.h>

#include "store/snapshot_codec.h"
#include "util/status.h"

/*
 * Combines a fresh record with the previously stored one (may be NULL).
 * Claude Code may leave a window out of one update; such a window is carried
 * over from `previous` only while it is still valid, i.e. its reset time is
 * known and later than `now`. Windows present in `incoming` always win.
 * The result takes as_of from `incoming`. Neither input is modified.
 *
 * LFCC_ERR_INVALID_ARG if `incoming` or `out` is NULL.
 */
lfcc_status_t snap_merge(const snap_record_t *incoming, const snap_record_t *previous, time_t now,
                         snap_record_t *out);

#endif /* LFCC_SNAPSHOT_MERGE_H */
