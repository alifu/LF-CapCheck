#include "testkit.h"

#include "store/snapshot_merge.h"

#define EPSILON 1e-9
#define NOW 1738400000
#define FIVE_HOUR_RESET 1738425600
#define SEVEN_DAY_RESET 1738857600

static snap_record_t record_with(time_t as_of, snap_window_t five_hour, snap_window_t seven_day)
{
    snap_record_t record = {0};

    record.as_of = as_of;
    record.five_hour = five_hour;
    record.seven_day = seven_day;
    return record;
}

static const snap_window_t NO_WINDOW = {false, 0.0, 0};

static void test_keeps_incoming_windows_when_there_is_no_previous_record(void)
{
    snap_record_t incoming = record_with(NOW, (snap_window_t){true, 10.0, FIVE_HOUR_RESET},
                                         (snap_window_t){true, 20.0, SEVEN_DAY_RESET});
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, NULL, NOW, &merged));

    CHECK_INT_EQ(NOW, merged.as_of);
    CHECK_DOUBLE_EQ(10.0, merged.five_hour.used_percentage, EPSILON);
    CHECK_DOUBLE_EQ(20.0, merged.seven_day.used_percentage, EPSILON);
}

static void test_incoming_windows_always_win_over_previous_ones(void)
{
    snap_record_t previous = record_with(NOW - 60, (snap_window_t){true, 90.0, FIVE_HOUR_RESET},
                                         (snap_window_t){true, 90.0, SEVEN_DAY_RESET});
    snap_record_t incoming = record_with(NOW, (snap_window_t){true, 10.0, FIVE_HOUR_RESET},
                                         (snap_window_t){true, 20.0, SEVEN_DAY_RESET});
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, &previous, NOW, &merged));

    CHECK_DOUBLE_EQ(10.0, merged.five_hour.used_percentage, EPSILON);
    CHECK_DOUBLE_EQ(20.0, merged.seven_day.used_percentage, EPSILON);
}

static void test_carries_forward_a_missing_window_that_has_not_reset_yet(void)
{
    snap_record_t previous = record_with(NOW - 60, (snap_window_t){true, 55.0, FIVE_HOUR_RESET},
                                         (snap_window_t){true, 30.0, SEVEN_DAY_RESET});
    snap_record_t incoming = record_with(NOW, NO_WINDOW,
                                         (snap_window_t){true, 31.0, SEVEN_DAY_RESET});
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, &previous, NOW, &merged));

    CHECK_INT_EQ(NOW - 60, merged.as_of); /* the carried window is that old: do not claim newer */
    CHECK(merged.five_hour.present);
    CHECK_DOUBLE_EQ(55.0, merged.five_hour.used_percentage, EPSILON);
    CHECK_INT_EQ(FIVE_HOUR_RESET, merged.five_hour.resets_at);
    CHECK_DOUBLE_EQ(31.0, merged.seven_day.used_percentage, EPSILON);
}

static void test_a_carried_window_keeps_the_oldest_capture_time_across_updates(void)
{
    snap_record_t first = record_with(NOW - 3600, (snap_window_t){true, 55.0, FIVE_HOUR_RESET},
                                      (snap_window_t){true, 30.0, SEVEN_DAY_RESET});
    snap_record_t second = record_with(NOW - 1800, NO_WINDOW, (snap_window_t){true, 31.0, SEVEN_DAY_RESET});
    snap_record_t third = record_with(NOW, NO_WINDOW, (snap_window_t){true, 32.0, SEVEN_DAY_RESET});
    snap_record_t merged_once = {0};
    snap_record_t merged_twice = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&second, &first, NOW - 1800, &merged_once));
    CHECK_INT_EQ(LFCC_OK, snap_merge(&third, &merged_once, NOW, &merged_twice));

    CHECK_INT_EQ(NOW - 3600, merged_twice.as_of); /* still the age of the 5-hour reading */
    CHECK_DOUBLE_EQ(55.0, merged_twice.five_hour.used_percentage, EPSILON);
    CHECK_DOUBLE_EQ(32.0, merged_twice.seven_day.used_percentage, EPSILON);
}

static void test_nothing_carried_means_the_incoming_capture_time_is_used(void)
{
    snap_record_t previous = record_with(NOW - 60, (snap_window_t){true, 55.0, FIVE_HOUR_RESET}, NO_WINDOW);
    snap_record_t incoming = record_with(NOW, (snap_window_t){true, 10.0, FIVE_HOUR_RESET}, NO_WINDOW);
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, &previous, NOW, &merged));

    CHECK_INT_EQ(NOW, merged.as_of);
}

static void test_does_not_carry_forward_a_window_that_has_already_reset(void)
{
    snap_record_t previous = record_with(NOW - 60, (snap_window_t){true, 55.0, NOW - 1},
                                         (snap_window_t){true, 30.0, SEVEN_DAY_RESET});
    snap_record_t exactly_now = record_with(NOW - 60, (snap_window_t){true, 55.0, NOW},
                                            (snap_window_t){true, 30.0, SEVEN_DAY_RESET});
    snap_record_t incoming = record_with(NOW, NO_WINDOW,
                                         (snap_window_t){true, 31.0, SEVEN_DAY_RESET});
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, &previous, NOW, &merged));
    CHECK(!merged.five_hour.present);

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, &exactly_now, NOW, &merged));
    CHECK(!merged.five_hour.present);
}

static void test_does_not_carry_forward_a_window_with_unknown_reset_time(void)
{
    snap_record_t previous = record_with(NOW - 60, (snap_window_t){true, 55.0, 0}, NO_WINDOW);
    snap_record_t incoming = record_with(NOW, NO_WINDOW,
                                         (snap_window_t){true, 31.0, SEVEN_DAY_RESET});
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, &previous, NOW, &merged));

    CHECK(!merged.five_hour.present);
}

static void test_inputs_are_not_modified(void)
{
    snap_record_t previous = record_with(NOW - 60, (snap_window_t){true, 55.0, FIVE_HOUR_RESET},
                                         NO_WINDOW);
    snap_record_t incoming = record_with(NOW, NO_WINDOW,
                                         (snap_window_t){true, 31.0, SEVEN_DAY_RESET});
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_OK, snap_merge(&incoming, &previous, NOW, &merged));

    CHECK(!incoming.five_hour.present);
    CHECK_INT_EQ(NOW - 60, previous.as_of);
    CHECK(!previous.seven_day.present);
}

static void test_rejects_null_arguments(void)
{
    snap_record_t incoming = record_with(NOW, NO_WINDOW,
                                         (snap_window_t){true, 31.0, SEVEN_DAY_RESET});
    snap_record_t merged = {0};

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_merge(NULL, NULL, NOW, &merged));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_merge(&incoming, NULL, NOW, NULL));
}

int main(void)
{
    RUN_TEST(test_keeps_incoming_windows_when_there_is_no_previous_record);
    RUN_TEST(test_incoming_windows_always_win_over_previous_ones);
    RUN_TEST(test_carries_forward_a_missing_window_that_has_not_reset_yet);
    RUN_TEST(test_a_carried_window_keeps_the_oldest_capture_time_across_updates);
    RUN_TEST(test_nothing_carried_means_the_incoming_capture_time_is_used);
    RUN_TEST(test_does_not_carry_forward_a_window_that_has_already_reset);
    RUN_TEST(test_does_not_carry_forward_a_window_with_unknown_reset_time);
    RUN_TEST(test_inputs_are_not_modified);
    RUN_TEST(test_rejects_null_arguments);
    return TESTKIT_RESULT();
}
