#include "testkit.h"

#include "ui/time_text.h"

static void check_age(long seconds, const char *expected)
{
    char text[64];

    CHECK_INT_EQ(LFCC_OK, time_text_age(seconds, text, sizeof text));
    CHECK_STR_EQ(expected, text);
}

static void test_age_uses_the_two_most_useful_units(void)
{
    check_age(0, "just now");
    check_age(59, "just now");
    check_age(60, "1 min ago");
    check_age(59 * 60 + 59, "59 min ago");
    check_age(3600, "1h 00m ago");
    check_age(3600 + 5 * 60, "1h 05m ago");
    check_age(23 * 3600 + 59 * 60, "23h 59m ago");
    check_age(86400, "1d ago");
    check_age(3 * 86400 + 7 * 3600, "3d ago");
}

static void test_age_in_the_future_counts_as_just_now(void)
{
    check_age(-1, "just now");
    check_age(-100000, "just now");
}

static void test_age_reports_a_small_buffer_and_bad_arguments(void)
{
    char tiny[5] = "abcd";

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, time_text_age(3 * 60, tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, time_text_age(60, NULL, 10));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, time_text_age(60, tiny, 0));
}

int main(void)
{
    RUN_TEST(test_age_uses_the_two_most_useful_units);
    RUN_TEST(test_age_in_the_future_counts_as_just_now);
    RUN_TEST(test_age_reports_a_small_buffer_and_bad_arguments);
    return TESTKIT_RESULT();
}
