#include "testkit.h"

#include "providers/claude_input.h"

#define EPSILON 1e-9
#define NOW 1738400000
#define FIXTURE_CAP 16384
#define SENTINEL_TIME 424242

static lfcc_status_t parse_text(const char *json, snap_record_t *out)
{
    return claude_input_parse(json, strlen(json), NOW, out);
}

static void test_parses_the_documented_full_payload_and_ignores_everything_else(void)
{
    char json[FIXTURE_CAP];
    long length = tk_read_file("tests/fixtures/statusline_full.json", json, sizeof json);
    snap_record_t record = {0};

    CHECK(length > 0);
    CHECK_INT_EQ(LFCC_OK, claude_input_parse(json, (size_t)length, NOW, &record));

    CHECK_INT_EQ(NOW, record.as_of);
    CHECK(record.five_hour.present);
    CHECK_DOUBLE_EQ(23.5, record.five_hour.used_percentage, EPSILON);
    CHECK_INT_EQ(1738425600, record.five_hour.resets_at);
    CHECK(record.seven_day.present);
    CHECK_DOUBLE_EQ(41.2, record.seven_day.used_percentage, EPSILON);
    CHECK_INT_EQ(1738857600, record.seven_day.resets_at);
}

static void test_payload_without_rate_limits_reports_not_found(void)
{
    char json[FIXTURE_CAP];
    long length = tk_read_file("tests/fixtures/statusline_no_rate_limits.json", json, sizeof json);
    snap_record_t record = {0};

    CHECK(length > 0);
    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, claude_input_parse(json, (size_t)length, NOW, &record));
}

static void test_reads_each_window_independently(void)
{
    snap_record_t only_weekly = {0};
    snap_record_t only_session = {0};

    CHECK_INT_EQ(LFCC_OK, parse_text("{\"rate_limits\":{\"seven_day\":"
                                     "{\"used_percentage\":10,\"resets_at\":1738857600}}}",
                                     &only_weekly));
    CHECK(!only_weekly.five_hour.present);
    CHECK(only_weekly.seven_day.present);

    CHECK_INT_EQ(LFCC_OK, parse_text("{\"rate_limits\":{\"five_hour\":"
                                     "{\"used_percentage\":20,\"resets_at\":1738425600}}}",
                                     &only_session));
    CHECK(only_session.five_hour.present);
    CHECK(!only_session.seven_day.present);
}

static void test_treats_empty_or_null_rate_limits_as_absent(void)
{
    static const char *const documents[] = {
        "{\"rate_limits\":{}}",
        "{\"rate_limits\":null}",
        "{\"rate_limits\":{\"spend_limit\":{\"used_percentage\":5,\"resets_at\":1738425600}}}",
        "{}",
    };

    for (size_t i = 0; i < sizeof documents / sizeof documents[0]; i++) {
        snap_record_t record = {0};

        CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, parse_text(documents[i], &record));
    }
}

static void test_clamps_out_of_range_percentages(void)
{
    snap_record_t record = {0};

    CHECK_INT_EQ(LFCC_OK, parse_text("{\"rate_limits\":{"
                                     "\"five_hour\":{\"used_percentage\":150,\"resets_at\":1738425600},"
                                     "\"seven_day\":{\"used_percentage\":-3,\"resets_at\":1738857600}}}",
                                     &record));

    CHECK_DOUBLE_EQ(100.0, record.five_hour.used_percentage, EPSILON);
    CHECK_DOUBLE_EQ(0.0, record.seven_day.used_percentage, EPSILON);
}

static void test_skips_a_malformed_window_but_keeps_the_good_one(void)
{
    static const char *const bad_five_hour[] = {
        "{\"used_percentage\":\"lots\",\"resets_at\":1738425600}",
        "{\"resets_at\":1738425600}",
        "{\"used_percentage\":1e999,\"resets_at\":1738425600}",
        "{\"used_percentage\":null,\"resets_at\":1738425600}",
        "42",
        "null",
        "[]",
    };

    for (size_t i = 0; i < sizeof bad_five_hour / sizeof bad_five_hour[0]; i++) {
        char json[256];
        snap_record_t record = {0};

        snprintf(json, sizeof json,
                 "{\"rate_limits\":{\"five_hour\":%s,"
                 "\"seven_day\":{\"used_percentage\":30,\"resets_at\":1738857600}}}",
                 bad_five_hour[i]);

        CHECK_INT_EQ(LFCC_OK, parse_text(json, &record));
        CHECK(!record.five_hour.present);
        CHECK(record.seven_day.present);
    }
}

static void test_reports_not_found_when_every_window_is_malformed(void)
{
    snap_record_t record = {0};

    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND,
                 parse_text("{\"rate_limits\":{\"five_hour\":{\"used_percentage\":\"x\"},"
                            "\"seven_day\":5}}",
                            &record));
}

static void test_unknown_reset_time_is_kept_as_zero(void)
{
    static const char *const resets[] = {"", ",\"resets_at\":-5", ",\"resets_at\":\"soon\"",
                                         ",\"resets_at\":99999999999999", ",\"resets_at\":null"};

    for (size_t i = 0; i < sizeof resets / sizeof resets[0]; i++) {
        char json[256];
        snap_record_t record = {0};

        snprintf(json, sizeof json,
                 "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":40%s}}}", resets[i]);

        CHECK_INT_EQ(LFCC_OK, parse_text(json, &record));
        CHECK(record.five_hour.present);
        CHECK_INT_EQ(0, record.five_hour.resets_at);
    }
}

static void test_rejects_malformed_json_and_wrong_shapes(void)
{
    static const char *const documents[] = {
        "", "not json", "{\"rate_limits\":{\"five_hour\":", "[]", "null", "42",
        "{\"rate_limits\":5}", "{\"rate_limits\":\"x\"}", "{\"rate_limits\":[]}",
    };

    for (size_t i = 0; i < sizeof documents / sizeof documents[0]; i++) {
        snap_record_t record = {0};

        CHECK_INT_EQ(LFCC_ERR_PARSE, parse_text(documents[i], &record));
    }
}

static void test_survives_deeply_nested_input(void)
{
    size_t depth = 200000;
    char *nested = malloc(depth);
    snap_record_t record = {0};

    CHECK(nested != NULL);
    memset(nested, '[', depth);
    CHECK_INT_EQ(LFCC_ERR_PARSE, claude_input_parse(nested, depth, NOW, &record));
    free(nested);
}

static void test_rejects_input_over_the_size_limit(void)
{
    size_t size = CLAUDE_INPUT_MAX_BYTES + 1;
    char *huge = malloc(size);
    snap_record_t record = {0};

    CHECK(huge != NULL);
    memset(huge, ' ', size);
    CHECK_INT_EQ(LFCC_ERR_TOO_LARGE, claude_input_parse(huge, size, NOW, &record));
    free(huge);
}

static void test_rejects_invalid_arguments(void)
{
    snap_record_t record = {0};
    static const char json[] = "{}";

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude_input_parse(NULL, 2, NOW, &record));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude_input_parse(json, 2, NOW, NULL));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude_input_parse(json, 2, 0, &record));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude_input_parse(json, 2, -1, &record));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude_input_parse(json, 2, SNAP_MAX_EPOCH + 1, &record));
}

static void test_leaves_output_untouched_on_failure(void)
{
    snap_record_t record = {0};

    record.as_of = SENTINEL_TIME;

    CHECK_INT_EQ(LFCC_ERR_PARSE, parse_text("garbage", &record));
    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, parse_text("{}", &record));
    CHECK_INT_EQ(SENTINEL_TIME, record.as_of);
}

int main(void)
{
    RUN_TEST(test_parses_the_documented_full_payload_and_ignores_everything_else);
    RUN_TEST(test_payload_without_rate_limits_reports_not_found);
    RUN_TEST(test_reads_each_window_independently);
    RUN_TEST(test_treats_empty_or_null_rate_limits_as_absent);
    RUN_TEST(test_clamps_out_of_range_percentages);
    RUN_TEST(test_skips_a_malformed_window_but_keeps_the_good_one);
    RUN_TEST(test_reports_not_found_when_every_window_is_malformed);
    RUN_TEST(test_unknown_reset_time_is_kept_as_zero);
    RUN_TEST(test_rejects_malformed_json_and_wrong_shapes);
    RUN_TEST(test_survives_deeply_nested_input);
    RUN_TEST(test_rejects_input_over_the_size_limit);
    RUN_TEST(test_rejects_invalid_arguments);
    RUN_TEST(test_leaves_output_untouched_on_failure);
    return TESTKIT_RESULT();
}
