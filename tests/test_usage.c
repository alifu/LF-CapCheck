#include "testkit.h"

#include <stdint.h>

#include "providers/usage.h"

#define EPSILON 1e-9
#define SENTINEL (-1.0)

static void test_data_older_than_half_an_hour_is_stale(void)
{
    const time_t now = 1738411200;
    usage_snapshot_t fresh = {0};
    usage_snapshot_t edge = {0};
    usage_snapshot_t stale = {0};
    usage_snapshot_t from_the_future = {0};

    fresh.as_of = now - 60;
    edge.as_of = now - USAGE_STALE_AFTER_SECONDS;
    stale.as_of = now - USAGE_STALE_AFTER_SECONDS - 1;
    from_the_future.as_of = now + 3600;

    CHECK(!usage_is_stale(&fresh, now));
    CHECK(!usage_is_stale(&edge, now)); /* exactly 30 minutes is still fine */
    CHECK(usage_is_stale(&stale, now));
    CHECK(!usage_is_stale(&from_the_future, now)); /* a clock skew is not staleness */
}

static void test_staleness_survives_extreme_timestamps(void)
{
    usage_snapshot_t snapshot = {0};

    snapshot.as_of = (time_t)INT64_MIN;
    CHECK(usage_is_stale(&snapshot, (time_t)INT64_MAX)); /* would overflow a plain subtraction */
    snapshot.as_of = (time_t)INT64_MAX;
    CHECK(!usage_is_stale(&snapshot, (time_t)INT64_MIN));
    CHECK(!usage_is_stale(NULL, 100));
}

static void test_converts_percent_to_fraction(void)
{
    double fraction = SENTINEL;

    CHECK_INT_EQ(LFCC_OK, usage_fraction_from_percent(23.5, &fraction));
    CHECK_DOUBLE_EQ(0.235, fraction, EPSILON);

    CHECK_INT_EQ(LFCC_OK, usage_fraction_from_percent(0.0, &fraction));
    CHECK_DOUBLE_EQ(0.0, fraction, EPSILON);

    CHECK_INT_EQ(LFCC_OK, usage_fraction_from_percent(100.0, &fraction));
    CHECK_DOUBLE_EQ(1.0, fraction, EPSILON);
}

static void test_clamps_out_of_range_percent_into_zero_to_one(void)
{
    double fraction = SENTINEL;

    CHECK_INT_EQ(LFCC_OK, usage_fraction_from_percent(150.0, &fraction));
    CHECK_DOUBLE_EQ(1.0, fraction, EPSILON);

    CHECK_INT_EQ(LFCC_OK, usage_fraction_from_percent(-5.0, &fraction));
    CHECK_DOUBLE_EQ(0.0, fraction, EPSILON);
}

static void test_rejects_non_finite_percent_and_leaves_output_untouched(void)
{
    double fraction = SENTINEL;

    CHECK_INT_EQ(LFCC_ERR_PARSE, usage_fraction_from_percent(NAN, &fraction));
    CHECK_DOUBLE_EQ(SENTINEL, fraction, EPSILON);

    CHECK_INT_EQ(LFCC_ERR_PARSE, usage_fraction_from_percent(INFINITY, &fraction));
    CHECK_DOUBLE_EQ(SENTINEL, fraction, EPSILON);

    CHECK_INT_EQ(LFCC_ERR_PARSE, usage_fraction_from_percent(-INFINITY, &fraction));
    CHECK_DOUBLE_EQ(SENTINEL, fraction, EPSILON);
}

static void test_rejects_null_output_pointer(void)
{
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, usage_fraction_from_percent(50.0, NULL));
}

static void test_clamp_percent_keeps_valid_values_and_clamps_the_rest(void)
{
    double percent = SENTINEL;

    CHECK_INT_EQ(LFCC_OK, usage_clamp_percent(23.5, &percent));
    CHECK_DOUBLE_EQ(23.5, percent, EPSILON);
    CHECK_INT_EQ(LFCC_OK, usage_clamp_percent(250.0, &percent));
    CHECK_DOUBLE_EQ(100.0, percent, EPSILON);
    CHECK_INT_EQ(LFCC_OK, usage_clamp_percent(-0.5, &percent));
    CHECK_DOUBLE_EQ(0.0, percent, EPSILON);
}

static void test_clamp_percent_rejects_non_finite_and_null(void)
{
    double percent = SENTINEL;

    CHECK_INT_EQ(LFCC_ERR_PARSE, usage_clamp_percent(NAN, &percent));
    CHECK_INT_EQ(LFCC_ERR_PARSE, usage_clamp_percent(INFINITY, &percent));
    CHECK_DOUBLE_EQ(SENTINEL, percent, EPSILON);
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, usage_clamp_percent(1.0, NULL));
}

static void test_percent_left_rounds_halves_up_on_the_percent_scale(void)
{
    static const struct {
        double used_percent;
        int left;
    } cases[] = {
        {0.0, 100},  {100.0, 0}, {23.5, 77}, {41.2, 59}, {62.0, 38}, {99.6, 0}, {0.4, 100},
        /* exact ties: 100 - x.5 = y.5 must round up to y + 1, not fall to y by float error */
        {42.5, 58},  {43.5, 57}, {54.5, 46}, {67.5, 33}, {77.5, 23}, {78.5, 22}, {80.5, 20},
        {90.5, 10},  {0.5, 100}, {99.5, 1},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CHECK_INT_EQ(cases[i].left, usage_percent_left(cases[i].used_percent / 100.0));
    }
}

static void test_percent_left_is_consistent_for_every_half_percent(void)
{
    for (int half_steps = 0; half_steps <= 200; half_steps++) {
        double used_percent = half_steps * 0.5;
        int expected = (int)(100.0 - used_percent + 0.5); /* exact for multiples of 0.5 */

        CHECK_INT_EQ(expected, usage_percent_left(used_percent / 100.0));
    }
}

static void test_percent_left_clamps_and_treats_non_finite_as_used_up(void)
{
    CHECK_INT_EQ(0, usage_percent_left(1.7));
    CHECK_INT_EQ(100, usage_percent_left(-0.4));
    CHECK_INT_EQ(0, usage_percent_left(NAN));
    CHECK_INT_EQ(0, usage_percent_left(INFINITY));
    CHECK_INT_EQ(0, usage_percent_left(-INFINITY));
}

static void test_all_expired_needs_at_least_one_window_and_all_of_them_expired(void)
{
    usage_snapshot_t snapshot = {0};

    CHECK(!usage_all_expired(&snapshot)); /* no windows */

    snapshot.window_count = 2;
    snapshot.windows[0].expired = true;
    snapshot.windows[1].expired = false;
    CHECK(!usage_all_expired(&snapshot));

    snapshot.windows[1].expired = true;
    CHECK(usage_all_expired(&snapshot));
    CHECK(!usage_all_expired(NULL));
}

int main(void)
{
    RUN_TEST(test_data_older_than_half_an_hour_is_stale);
    RUN_TEST(test_staleness_survives_extreme_timestamps);
    RUN_TEST(test_percent_left_rounds_halves_up_on_the_percent_scale);
    RUN_TEST(test_percent_left_is_consistent_for_every_half_percent);
    RUN_TEST(test_percent_left_clamps_and_treats_non_finite_as_used_up);
    RUN_TEST(test_all_expired_needs_at_least_one_window_and_all_of_them_expired);
    RUN_TEST(test_converts_percent_to_fraction);
    RUN_TEST(test_clamps_out_of_range_percent_into_zero_to_one);
    RUN_TEST(test_rejects_non_finite_percent_and_leaves_output_untouched);
    RUN_TEST(test_rejects_null_output_pointer);
    RUN_TEST(test_clamp_percent_keeps_valid_values_and_clamps_the_rest);
    RUN_TEST(test_clamp_percent_rejects_non_finite_and_null);
    return TESTKIT_RESULT();
}
