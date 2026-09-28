#include "testkit.h"

#include "providers/usage.h"

#define EPSILON 1e-9
#define SENTINEL (-1.0)

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

int main(void)
{
    RUN_TEST(test_converts_percent_to_fraction);
    RUN_TEST(test_clamps_out_of_range_percent_into_zero_to_one);
    RUN_TEST(test_rejects_non_finite_percent_and_leaves_output_untouched);
    RUN_TEST(test_rejects_null_output_pointer);
    return TESTKIT_RESULT();
}
