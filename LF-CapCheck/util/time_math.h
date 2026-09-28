#ifndef LFCC_TIME_MATH_H
#define LFCC_TIME_MATH_H

#include <time.h>

#define SECONDS_PER_MINUTE 60L
#define SECONDS_PER_HOUR 3600L
#define SECONDS_PER_DAY 86400L

/*
 * `to - from` in seconds, saturated to LONG_MIN/LONG_MAX instead of
 * overflowing (signed overflow is undefined behaviour), so any pair of
 * timestamps, however corrupt, is safe to compare.
 */
long time_seconds_between(time_t from, time_t to);

#endif /* LFCC_TIME_MATH_H */
