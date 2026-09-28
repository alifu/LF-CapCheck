#include "testkit.h"

#include "store/snapshot_codec.h"

#define EPSILON 1e-9
#define JSON_CAP 512
#define AS_OF 1700000000
#define FIVE_HOUR_RESET 1738425600
#define SEVEN_DAY_RESET 1738857600

static snap_record_t sample_record(void)
{
    snap_record_t record = {0};

    record.as_of = AS_OF;
    record.five_hour = (snap_window_t){true, 23.5, FIVE_HOUR_RESET};
    record.seven_day = (snap_window_t){true, 41.2, SEVEN_DAY_RESET};
    return record;
}

static void check_windows_equal(const snap_window_t *expected, const snap_window_t *actual)
{
    CHECK(expected->present == actual->present);
    if (!expected->present) {
        return; /* the values of an absent window carry no meaning */
    }
    CHECK_DOUBLE_EQ(expected->used_percentage, actual->used_percentage, EPSILON);
    CHECK_INT_EQ(expected->resets_at, actual->resets_at);
}

static void check_round_trip(const snap_record_t *record)
{
    char json[JSON_CAP];
    size_t length = 0;
    snap_record_t decoded = {0};

    CHECK_INT_EQ(LFCC_OK, snap_encode(record, json, sizeof json, &length));
    CHECK_INT_EQ(strlen(json), length);
    CHECK_INT_EQ(LFCC_OK, snap_decode(json, length, &decoded));

    CHECK_INT_EQ(record->as_of, decoded.as_of);
    check_windows_equal(&record->five_hour, &decoded.five_hour);
    check_windows_equal(&record->seven_day, &decoded.seven_day);
}

static void test_round_trips_both_windows(void)
{
    snap_record_t record = sample_record();

    check_round_trip(&record);
}

static void test_round_trips_a_single_window(void)
{
    snap_record_t only_weekly = sample_record();
    snap_record_t only_session = sample_record();

    only_weekly.five_hour.present = false;
    only_session.seven_day.present = false;

    check_round_trip(&only_weekly);
    check_round_trip(&only_session);
}

static void test_round_trips_boundary_values(void)
{
    snap_record_t record = sample_record();

    record.five_hour.used_percentage = 0.0;
    record.seven_day.used_percentage = 100.0;
    record.seven_day.resets_at = 0; /* unknown reset time */

    check_round_trip(&record);
}

static void test_encoded_text_is_one_line_of_versioned_json(void)
{
    snap_record_t record = sample_record();
    char json[JSON_CAP];
    size_t length = 0;

    CHECK_INT_EQ(LFCC_OK, snap_encode(&record, json, sizeof json, &length));

    CHECK_CONTAINS(json, "\"version\":1");
    CHECK_CONTAINS(json, "\"five_hour\"");
    CHECK_CONTAINS(json, "\"seven_day\"");
    CHECK(json[length - 1] == '\n');
    CHECK(strchr(json, '\n') == &json[length - 1]);
}

static void test_encode_omits_absent_windows(void)
{
    snap_record_t record = sample_record();
    char json[JSON_CAP];
    size_t length = 0;

    record.five_hour.present = false;

    CHECK_INT_EQ(LFCC_OK, snap_encode(&record, json, sizeof json, &length));
    CHECK(strstr(json, "five_hour") == NULL);
    CHECK_CONTAINS(json, "seven_day");
}

static void test_encode_rejects_invalid_records(void)
{
    char json[JSON_CAP];
    size_t length = 0;
    snap_record_t none = sample_record();
    snap_record_t not_a_number = sample_record();
    snap_record_t infinite = sample_record();
    snap_record_t negative = sample_record();
    snap_record_t over_hundred = sample_record();
    snap_record_t negative_reset = sample_record();
    snap_record_t no_capture_time = sample_record();
    snap_record_t far_future = sample_record();

    none.five_hour.present = false;
    none.seven_day.present = false;
    not_a_number.five_hour.used_percentage = NAN;
    infinite.seven_day.used_percentage = INFINITY;
    negative.five_hour.used_percentage = -0.1;
    over_hundred.seven_day.used_percentage = 100.1;
    negative_reset.five_hour.resets_at = -1;
    no_capture_time.as_of = 0;
    far_future.as_of = SNAP_MAX_EPOCH + 1;

    const snap_record_t *invalid[] = {&none, &not_a_number, &infinite, &negative,
                                      &over_hundred, &negative_reset, &no_capture_time,
                                      &far_future};

    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_encode(invalid[i], json, sizeof json, &length));
    }
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_encode(NULL, json, sizeof json, &length));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_encode(&none, NULL, sizeof json, &length));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_encode(&none, json, sizeof json, NULL));
}

static void test_encode_reports_a_buffer_that_is_too_small(void)
{
    snap_record_t record = sample_record();
    char json[JSON_CAP];
    char tight[JSON_CAP];
    size_t length = 0;

    CHECK_INT_EQ(LFCC_OK, snap_encode(&record, json, sizeof json, &length));

    /* Needs length + 1 bytes for the terminator. */
    CHECK_INT_EQ(LFCC_ERR_CAPACITY, snap_encode(&record, tight, length, &length));
    CHECK_INT_EQ(LFCC_ERR_CAPACITY, snap_encode(&record, tight, 10, &length));
    CHECK_INT_EQ(LFCC_ERR_CAPACITY, snap_encode(&record, tight, 0, &length));
    CHECK_INT_EQ(LFCC_OK, snap_encode(&record, tight, strlen(json) + 1, &length));
}

static void test_decode_rejects_malformed_documents(void)
{
    static const char *const documents[] = {
        "",
        "not json",
        "{\"version\":1,\"as_of\":17000",                       /* truncated */
        "[]",
        "null",
        "42",
        "{}",
        "{\"version\":1}",                                      /* no as_of, no windows */
        "{\"version\":1,\"as_of\":1700000000}",                 /* no windows */
        "{\"version\":1,\"as_of\":\"x\",\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":-5,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":99999999999,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":5}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":\"a\",\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":150,\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":-1,\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1e999,\"resets_at\":2}}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":-2}}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1}}",
        "{\"version\":1,\"as_of\":1700000000,\"seven_day\":null}",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}} trailing",
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}{}",
    };

    for (size_t i = 0; i < sizeof documents / sizeof documents[0]; i++) {
        snap_record_t decoded = {0};

        CHECK_INT_EQ(LFCC_ERR_PARSE, snap_decode(documents[i], strlen(documents[i]), &decoded));
    }
}

static void test_decode_rejects_an_unsupported_version(void)
{
    static const char *const documents[] = {
        "{\"version\":2,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
        "{\"version\":0,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
        "{\"version\":\"1\",\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
        "{\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
    };

    for (size_t i = 0; i < sizeof documents / sizeof documents[0]; i++) {
        snap_record_t decoded = {0};

        CHECK_INT_EQ(LFCC_ERR_PARSE, snap_decode(documents[i], strlen(documents[i]), &decoded));
    }
}

static void test_decode_ignores_unknown_fields(void)
{
    static const char json[] =
        "{\"version\":1,\"as_of\":1700000000,\"extra\":{\"a\":[1,2,3]},"
        "\"five_hour\":{\"used_percentage\":10,\"resets_at\":20,\"note\":\"x\"}}";
    snap_record_t decoded = {0};

    CHECK_INT_EQ(LFCC_OK, snap_decode(json, strlen(json), &decoded));
    CHECK(decoded.five_hour.present);
    CHECK(!decoded.seven_day.present);
    CHECK_DOUBLE_EQ(10.0, decoded.five_hour.used_percentage, EPSILON);
}

static void test_decode_honours_the_length_not_the_terminator(void)
{
    static const char json[] =
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":10,\"resets_at\":20}}";
    char padded[JSON_CAP];
    snap_record_t decoded = {0};
    size_t length = strlen(json);

    memset(padded, 'z', sizeof padded);
    memcpy(padded, json, length);

    CHECK_INT_EQ(LFCC_OK, snap_decode(padded, length, &decoded));
    CHECK_INT_EQ(LFCC_ERR_PARSE, snap_decode(padded, length - 1, &decoded));
}

static void test_decode_rejects_embedded_nul_bytes(void)
{
    /* Bytes after a NUL would otherwise be invisible to the parser. */
    static const char json[] =
        "{\"version\":1,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}"
        "\0{\"injected\":true}";
    snap_record_t decoded = {0};

    CHECK_INT_EQ(LFCC_ERR_PARSE, snap_decode(json, sizeof json - 1, &decoded));
}

static void test_decode_rejects_oversized_input_before_parsing(void)
{
    char *huge = malloc(SNAP_MAX_FILE_BYTES + 1);
    snap_record_t decoded = {0};

    CHECK(huge != NULL);
    memset(huge, ' ', SNAP_MAX_FILE_BYTES + 1);
    CHECK_INT_EQ(LFCC_ERR_TOO_LARGE, snap_decode(huge, SNAP_MAX_FILE_BYTES + 1, &decoded));
    free(huge);
}

static void test_decode_survives_deeply_nested_input(void)
{
    char nested[SNAP_MAX_FILE_BYTES];
    snap_record_t decoded = {0};

    memset(nested, '[', sizeof nested);

    CHECK_INT_EQ(LFCC_ERR_PARSE, snap_decode(nested, sizeof nested, &decoded));
}

static void test_decode_leaves_output_untouched_on_failure(void)
{
    snap_record_t decoded = sample_record();

    CHECK_INT_EQ(LFCC_ERR_PARSE, snap_decode("garbage", 7, &decoded));

    check_windows_equal(&(snap_window_t){true, 23.5, FIVE_HOUR_RESET}, &decoded.five_hour);
    CHECK_INT_EQ(AS_OF, decoded.as_of);
}

static void test_decode_rejects_null_arguments(void)
{
    snap_record_t decoded = {0};

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_decode(NULL, 5, &decoded));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_decode("{}", 2, NULL));
}

int main(void)
{
    RUN_TEST(test_round_trips_both_windows);
    RUN_TEST(test_round_trips_a_single_window);
    RUN_TEST(test_round_trips_boundary_values);
    RUN_TEST(test_encoded_text_is_one_line_of_versioned_json);
    RUN_TEST(test_encode_omits_absent_windows);
    RUN_TEST(test_encode_rejects_invalid_records);
    RUN_TEST(test_encode_reports_a_buffer_that_is_too_small);
    RUN_TEST(test_decode_rejects_malformed_documents);
    RUN_TEST(test_decode_rejects_an_unsupported_version);
    RUN_TEST(test_decode_ignores_unknown_fields);
    RUN_TEST(test_decode_honours_the_length_not_the_terminator);
    RUN_TEST(test_decode_rejects_embedded_nul_bytes);
    RUN_TEST(test_decode_rejects_oversized_input_before_parsing);
    RUN_TEST(test_decode_survives_deeply_nested_input);
    RUN_TEST(test_decode_leaves_output_untouched_on_failure);
    RUN_TEST(test_decode_rejects_null_arguments);
    return TESTKIT_RESULT();
}
