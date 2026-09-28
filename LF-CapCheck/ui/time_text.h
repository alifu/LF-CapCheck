#ifndef LFCC_TIME_TEXT_H
#define LFCC_TIME_TEXT_H

#include <stddef.h>

#include "util/status.h"

/*
 * How long ago something happened, in words:
 * "just now" (< 1 min, or in the future), "3 min ago", "1h 05m ago", "2d ago".
 * LFCC_ERR_INVALID_ARG for a NULL/zero-size buffer; LFCC_ERR_CAPACITY if it is
 * too small (out becomes "").
 */
lfcc_status_t time_text_age(long seconds, char *out, size_t cap);

#endif /* LFCC_TIME_TEXT_H */
