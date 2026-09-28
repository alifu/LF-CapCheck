#ifndef LFCC_CLAUDE_INPUT_H
#define LFCC_CLAUDE_INPUT_H

#include <stddef.h>
#include <time.h>

#include "store/snapshot_codec.h"
#include "util/status.h"

/* Claude Code's status line payload is a few KB; anything above this is ignored. */
#define CLAUDE_INPUT_MAX_BYTES (1024 * 1024)

/*
 * Extracts the subscription rate limits from the JSON that Claude Code sends
 * to a status line command (documented at code.claude.com/docs/en/statusline).
 * Only `rate_limits.five_hour` and `rate_limits.seven_day` are read; nothing
 * else in the payload (paths, session data, costs) is looked at or kept.
 *
 * Each window is read independently. A window that is absent or malformed is
 * left out; a percentage outside 0..100 is clamped; an unusable reset time
 * becomes 0 (unknown). `now` becomes record->as_of.
 *
 * LFCC_OK: at least one window was read.
 * LFCC_ERR_NOT_FOUND: valid JSON without usable rate limits (not a Pro/Max
 *   user, or before the first API response).
 * LFCC_ERR_PARSE: not JSON, not an object, or rate_limits has the wrong type.
 * LFCC_ERR_TOO_LARGE: more than CLAUDE_INPUT_MAX_BYTES.
 * LFCC_ERR_INVALID_ARG: NULL argument or `now` outside 1..SNAP_MAX_EPOCH.
 * *out is untouched unless LFCC_OK.
 */
lfcc_status_t claude_input_parse(const char *json, size_t length, time_t now, snap_record_t *out);

#endif /* LFCC_CLAUDE_INPUT_H */
