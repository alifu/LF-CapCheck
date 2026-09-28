#include "testkit.h"

#include <sys/stat.h>
#include <unistd.h>

#include "cli/statusline.h"
#include "providers/claude_input.h"
#include "store/snapshot_store.h"
#include "util/log.h"
#include "util/secure_path.h"

#define NOW 1738400000
#define FIVE_HOUR_RESET 1738425600
#define SEVEN_DAY_RESET 1738857600
#define FIXTURE_CAP 16384
#define OUT_CAP 1024
#define PATH_CAP 512
#define EPSILON 1e-9
#define SEP "\xc2\xb7" /* middle dot, UTF-8 */

/* ---- formatter ---- */

static snap_record_t record_of(snap_window_t five_hour, snap_window_t seven_day)
{
    snap_record_t record = {0};

    record.as_of = NOW;
    record.five_hour = five_hour;
    record.seven_day = seven_day;
    return record;
}

static void test_format_shows_remaining_percent_for_both_windows(void)
{
    snap_record_t record = record_of((snap_window_t){true, 23.5, FIVE_HOUR_RESET},
                                     (snap_window_t){true, 41.2, SEVEN_DAY_RESET});
    char line[OUT_CAP];

    CHECK_INT_EQ(LFCC_OK, statusline_format(&record, NOW, line, sizeof line));

    CHECK_STR_EQ("5h 77% left " SEP " 7d 59% left", line);
}

static void test_format_shows_only_the_windows_that_are_present(void)
{
    snap_record_t weekly_only = record_of((snap_window_t){false, 0, 0},
                                          (snap_window_t){true, 41.2, SEVEN_DAY_RESET});
    snap_record_t session_only = record_of((snap_window_t){true, 10.0, FIVE_HOUR_RESET},
                                           (snap_window_t){false, 0, 0});
    char line[OUT_CAP];

    CHECK_INT_EQ(LFCC_OK, statusline_format(&weekly_only, NOW, line, sizeof line));
    CHECK_STR_EQ("7d 59% left", line);
    CHECK_INT_EQ(LFCC_OK, statusline_format(&session_only, NOW, line, sizeof line));
    CHECK_STR_EQ("5h 90% left", line);
}

static void test_format_handles_the_extremes(void)
{
    snap_record_t record = record_of((snap_window_t){true, 0.0, FIVE_HOUR_RESET},
                                     (snap_window_t){true, 100.0, SEVEN_DAY_RESET});
    char line[OUT_CAP];

    CHECK_INT_EQ(LFCC_OK, statusline_format(&record, NOW, line, sizeof line));

    CHECK_STR_EQ("5h 100% left " SEP " 7d 0% left", line);
}

static void test_format_omits_windows_that_have_already_reset(void)
{
    snap_record_t record = record_of((snap_window_t){true, 80.0, NOW},
                                     (snap_window_t){true, 41.2, SEVEN_DAY_RESET});
    char line[OUT_CAP];

    CHECK_INT_EQ(LFCC_OK, statusline_format(&record, NOW, line, sizeof line));

    CHECK_STR_EQ("7d 59% left", line);
}

static void test_format_is_empty_when_nothing_is_showable(void)
{
    snap_record_t none = record_of((snap_window_t){false, 0, 0}, (snap_window_t){false, 0, 0});
    char line[OUT_CAP] = "junk";

    CHECK_INT_EQ(LFCC_OK, statusline_format(&none, NOW, line, sizeof line));

    CHECK_STR_EQ("", line);
}

static void test_format_reports_a_buffer_that_is_too_small(void)
{
    snap_record_t record = record_of((snap_window_t){true, 23.5, FIVE_HOUR_RESET},
                                     (snap_window_t){true, 41.2, SEVEN_DAY_RESET});
    char tiny[8] = "junk";

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, statusline_format(&record, NOW, tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, statusline_format(NULL, NOW, tiny, sizeof tiny));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, statusline_format(&record, NOW, NULL, 10));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, statusline_format(&record, NOW, tiny, 0));
}

/* ---- statusline_run, end to end ---- */

typedef struct {
    int exit_code;
    char out[OUT_CAP];
    char err[OUT_CAP];
} run_result_t;

static run_result_t run_with_input(const char *data_dir, const void *input, size_t length,
                                   time_t now)
{
    run_result_t result = {0};
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    cli_io_t io = {in, out, err, data_dir, now, NULL, {80, false, false}};

    CHECK(fwrite(input, 1, length, in) == length);
    rewind(in);
    log_set_stream(err);
    result.exit_code = statusline_run(&io);
    log_set_stream(NULL);

    tk_read_all(out, result.out, sizeof result.out);
    tk_read_all(err, result.err, sizeof result.err);
    fclose(in);
    fclose(out);
    fclose(err);
    return result;
}

static run_result_t run_text(const char *data_dir, const char *text, time_t now)
{
    return run_with_input(data_dir, text, strlen(text), now);
}

static void make_data_dir(char *path, size_t cap)
{
    CHECK(tk_make_temp_dir(path, cap) == 0);
}

static lfcc_status_t read_snapshot(const char *data_dir, snap_record_t *out)
{
    int fd = -1;
    lfcc_status_t status = secure_dir_open(data_dir, &fd);

    if (status == LFCC_OK) {
        status = snap_read(fd, out);
        close(fd);
    }
    return status;
}

static void test_saves_the_snapshot_and_prints_one_status_line(void)
{
    char data_dir[PATH_CAP];
    char json[FIXTURE_CAP];
    long length = tk_read_file("tests/fixtures/statusline_full.json", json, sizeof json);
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    CHECK(length > 0);

    result = run_with_input(data_dir, json, (size_t)length, NOW);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("5h 77% left " SEP " 7d 59% left\n", result.out);
    CHECK_STR_EQ("", result.err);
    CHECK_INT_EQ(LFCC_OK, read_snapshot(data_dir, &saved));
    CHECK_INT_EQ(NOW, saved.as_of);
    CHECK_DOUBLE_EQ(23.5, saved.five_hour.used_percentage, EPSILON);
    CHECK_INT_EQ(SEVEN_DAY_RESET, saved.seven_day.resets_at);

    tk_remove_dir(data_dir);
}

static void test_stores_nothing_but_the_rate_limits(void)
{
    char data_dir[PATH_CAP];
    char json[FIXTURE_CAP];
    char path[PATH_CAP];
    char stored[SNAP_MAX_FILE_BYTES + 1];
    long length = tk_read_file("tests/fixtures/statusline_full.json", json, sizeof json);

    make_data_dir(data_dir, sizeof data_dir);
    CHECK(length > 0);
    run_with_input(data_dir, json, (size_t)length, NOW);

    snprintf(path, sizeof path, "%s/%s", data_dir, SNAP_FILE_NAME);
    CHECK(tk_read_file(path, stored, sizeof stored) > 0);
    CHECK(strstr(stored, "/Users/example") == NULL);  /* no paths */
    CHECK(strstr(stored, "abc123") == NULL);          /* no session id */
    CHECK(strstr(stored, "spend_limit") == NULL);
    CHECK(strstr(stored, "transcript") == NULL);

    tk_remove_dir(data_dir);
}

static void test_without_rate_limits_it_prints_nothing_and_writes_nothing(void)
{
    char data_dir[PATH_CAP];
    char json[FIXTURE_CAP];
    long length = tk_read_file("tests/fixtures/statusline_no_rate_limits.json", json, sizeof json);
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    CHECK(length > 0);

    result = run_with_input(data_dir, json, (size_t)length, NOW);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("", result.out);
    CHECK_STR_EQ("", result.err);
    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, read_snapshot(data_dir, &saved));

    tk_remove_dir(data_dir);
}

static void test_garbage_input_exits_zero_silently_and_keeps_the_previous_snapshot(void)
{
    static const char valid[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":10,\"resets_at\":1738425600}}}";
    static const char *const garbage[] = {"", "not json", "{\"rate_limits\":", "[1,2,3]",
                                          "{\"rate_limits\":5}"};
    char data_dir[PATH_CAP];
    snap_record_t saved = {0};

    make_data_dir(data_dir, sizeof data_dir);
    run_text(data_dir, valid, NOW);

    for (size_t i = 0; i < sizeof garbage / sizeof garbage[0]; i++) {
        run_result_t result = run_text(data_dir, garbage[i], NOW + 60);

        CHECK_INT_EQ(0, result.exit_code);
        CHECK_STR_EQ("", result.out);
        CHECK_STR_EQ("", result.err);
    }

    CHECK_INT_EQ(LFCC_OK, read_snapshot(data_dir, &saved));
    CHECK_INT_EQ(NOW, saved.as_of);
    CHECK_DOUBLE_EQ(10.0, saved.five_hour.used_percentage, EPSILON);

    tk_remove_dir(data_dir);
}

static void test_oversized_input_is_ignored_without_output(void)
{
    char data_dir[PATH_CAP];
    size_t size = CLAUDE_INPUT_MAX_BYTES + 100;
    char *huge = malloc(size);
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    CHECK(huge != NULL);
    memset(huge, ' ', size);

    result = run_with_input(data_dir, huge, size, NOW);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("", result.out);
    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, read_snapshot(data_dir, &saved));

    free(huge);
    tk_remove_dir(data_dir);
}

static void test_a_window_missing_from_a_later_update_is_carried_forward(void)
{
    static const char first[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":55,\"resets_at\":1738425600},"
        "\"seven_day\":{\"used_percentage\":30,\"resets_at\":1738857600}}}";
    static const char later[] =
        "{\"rate_limits\":{\"seven_day\":{\"used_percentage\":31,\"resets_at\":1738857600}}}";
    char data_dir[PATH_CAP];
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    run_text(data_dir, first, NOW);

    result = run_text(data_dir, later, NOW + 60);

    CHECK_STR_EQ("5h 45% left " SEP " 7d 69% left\n", result.out);
    CHECK_INT_EQ(LFCC_OK, read_snapshot(data_dir, &saved));
    CHECK_INT_EQ(NOW + 60, saved.as_of);
    CHECK(saved.five_hour.present);
    CHECK_DOUBLE_EQ(55.0, saved.five_hour.used_percentage, EPSILON);
    CHECK_DOUBLE_EQ(31.0, saved.seven_day.used_percentage, EPSILON);

    tk_remove_dir(data_dir);
}

static void test_a_window_that_has_reset_is_not_carried_forward(void)
{
    static const char first[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":55,\"resets_at\":1738425600},"
        "\"seven_day\":{\"used_percentage\":30,\"resets_at\":1738857600}}}";
    static const char later[] =
        "{\"rate_limits\":{\"seven_day\":{\"used_percentage\":31,\"resets_at\":1738857600}}}";
    char data_dir[PATH_CAP];
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    run_text(data_dir, first, NOW);

    result = run_text(data_dir, later, FIVE_HOUR_RESET + 10);

    CHECK_STR_EQ("7d 69% left\n", result.out);
    CHECK_INT_EQ(LFCC_OK, read_snapshot(data_dir, &saved));
    CHECK(!saved.five_hour.present);

    tk_remove_dir(data_dir);
}

static void test_a_corrupt_previous_snapshot_does_not_stop_the_update(void)
{
    static const char update[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":20,\"resets_at\":1738425600}}}";
    char data_dir[PATH_CAP];
    char path[PATH_CAP];
    FILE *broken = NULL;
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    snprintf(path, sizeof path, "%s/%s", data_dir, SNAP_FILE_NAME);
    broken = fopen(path, "w");
    CHECK(broken != NULL);
    fputs("corrupt", broken);
    fclose(broken);
    chmod(path, 0600);

    result = run_text(data_dir, update, NOW);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("5h 80% left\n", result.out);
    CHECK_INT_EQ(LFCC_OK, read_snapshot(data_dir, &saved));

    tk_remove_dir(data_dir);
}

static void test_storage_failure_still_prints_the_line_and_warns_on_stderr(void)
{
    static const char update[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":20,\"resets_at\":1738425600}}}";
    char data_dir[PATH_CAP];
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    CHECK(chmod(data_dir, 0755) == 0); /* group/other access: refused by secure_dir_open */

    result = run_text(data_dir, update, NOW);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("5h 80% left\n", result.out);
    CHECK_CONTAINS(result.err, "lf-capcheck: warning:");

    tk_remove_dir(data_dir);
}

int main(void)
{
    RUN_TEST(test_format_shows_remaining_percent_for_both_windows);
    RUN_TEST(test_format_shows_only_the_windows_that_are_present);
    RUN_TEST(test_format_handles_the_extremes);
    RUN_TEST(test_format_omits_windows_that_have_already_reset);
    RUN_TEST(test_format_is_empty_when_nothing_is_showable);
    RUN_TEST(test_format_reports_a_buffer_that_is_too_small);
    RUN_TEST(test_saves_the_snapshot_and_prints_one_status_line);
    RUN_TEST(test_stores_nothing_but_the_rate_limits);
    RUN_TEST(test_without_rate_limits_it_prints_nothing_and_writes_nothing);
    RUN_TEST(test_garbage_input_exits_zero_silently_and_keeps_the_previous_snapshot);
    RUN_TEST(test_oversized_input_is_ignored_without_output);
    RUN_TEST(test_a_window_missing_from_a_later_update_is_carried_forward);
    RUN_TEST(test_a_window_that_has_reset_is_not_carried_forward);
    RUN_TEST(test_a_corrupt_previous_snapshot_does_not_stop_the_update);
    RUN_TEST(test_storage_failure_still_prints_the_line_and_warns_on_stderr);
    return TESTKIT_RESULT();
}
