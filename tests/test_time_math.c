#include "testkit.h"

#include <limits.h>
#include <stdint.h>

#include "util/time_math.h"

static void test_returns_the_plain_difference_in_seconds(void)
{
    CHECK_INT_EQ(60, time_seconds_between(1000, 1060));
    CHECK_INT_EQ(0, time_seconds_between(1000, 1000));
    CHECK_INT_EQ(-60, time_seconds_between(1060, 1000));
    CHECK_INT_EQ(2, time_seconds_between(-1, 1));
}

static void test_saturates_instead_of_overflowing(void)
{
    CHECK(time_seconds_between(-1, (time_t)INT64_MAX) == LONG_MAX);
    CHECK(time_seconds_between((time_t)INT64_MIN, 1) == LONG_MAX);
    CHECK(time_seconds_between((time_t)INT64_MIN, (time_t)INT64_MAX) == LONG_MAX);
    CHECK(time_seconds_between(1, (time_t)INT64_MIN) == LONG_MIN);
    CHECK(time_seconds_between((time_t)INT64_MAX, -1) == LONG_MIN);
    CHECK(time_seconds_between((time_t)INT64_MAX, (time_t)INT64_MIN) == LONG_MIN);
}

static void test_handles_the_extremes_that_do_not_overflow(void)
{
    CHECK(time_seconds_between((time_t)INT64_MAX, (time_t)INT64_MAX) == 0);
    CHECK(time_seconds_between((time_t)INT64_MIN, (time_t)INT64_MIN) == 0);
    CHECK(time_seconds_between(0, (time_t)INT64_MAX) == LONG_MAX);
}

int main(void)
{
    RUN_TEST(test_returns_the_plain_difference_in_seconds);
    RUN_TEST(test_saturates_instead_of_overflowing);
    RUN_TEST(test_handles_the_extremes_that_do_not_overflow);
    return TESTKIT_RESULT();
}
