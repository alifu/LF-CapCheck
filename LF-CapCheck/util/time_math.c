#include "util/time_math.h"

#include <limits.h>

long time_seconds_between(time_t from, time_t to)
{
    long difference = 0;

    if (__builtin_sub_overflow(to, from, &difference)) {
        return to > from ? LONG_MAX : LONG_MIN;
    }
    return difference;
}
