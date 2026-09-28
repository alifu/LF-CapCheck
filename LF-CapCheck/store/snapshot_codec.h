#ifndef LFCC_SNAPSHOT_CODEC_H
#define LFCC_SNAPSHOT_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "util/status.h"

#define SNAP_VERSION 1
#define SNAP_MAX_FILE_BYTES 4096
#define SNAP_MAX_EPOCH 4102444800LL /* 2100-01-01: anything later is treated as corrupt */

/* One rate-limit window as reported by Claude Code. */
typedef struct {
    bool present;
    double used_percentage; /* 0..100 */
    time_t resets_at;       /* epoch seconds, 0 = unknown */
} snap_window_t;

/* What the `statusline` command saves and the menu reads back. */
typedef struct {
    time_t as_of; /* when Claude Code reported the data */
    snap_window_t five_hour;
    snap_window_t seven_day;
} snap_record_t;

/*
 * Writes the record as one line of versioned JSON, NUL-terminated, into
 * `out`; *out_len excludes the terminator.
 * LFCC_ERR_INVALID_ARG: NULL argument, no window present, or a value out of
 * range (percentage outside 0..100 or non-finite, negative or absurd times).
 * LFCC_ERR_CAPACITY: `cap` is smaller than the text plus terminator.
 */
lfcc_status_t snap_encode(const snap_record_t *record, char *out, size_t cap, size_t *out_len);

/*
 * Parses and strictly validates `length` bytes of JSON (no terminator needed).
 * Unknown fields are ignored. LFCC_ERR_TOO_LARGE above SNAP_MAX_FILE_BYTES;
 * LFCC_ERR_PARSE for anything malformed, trailing data, embedded NUL bytes,
 * an unsupported version or out-of-range values. *out is untouched on error.
 */
lfcc_status_t snap_decode(const char *json, size_t length, snap_record_t *out);

#endif /* LFCC_SNAPSHOT_CODEC_H */
