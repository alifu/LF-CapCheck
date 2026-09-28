#include "testkit.h"

#include <sys/stat.h>
#include <unistd.h>
#include <util.h> /* openpty */

#include "cli/statusline.h"
#include "providers/claude_input.h"
#include "store/snapshot_store.h"
#include "ui/chart.h"
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

/* The status line and the chart must always print the same number for the same data. */
static void test_status_line_and_chart_agree_on_the_percentage_left(void)
{
    for (int half_steps = 0; half_steps <= 200; half_steps++) {
        double used_percent = half_steps * 0.5;
        snap_record_t record = record_of((snap_window_t){true, used_percent, FIVE_HOUR_RESET},
                                         (snap_window_t){false, 0, 0});
        usage_snapshot_t usage = {0};
        chart_options_t options = {80, false, true, NOW, "Test", NULL};
        char line[OUT_CAP];
        char chart[OUT_CAP];
        char expected_line[32];
        char expected_chart[32];

        /* The chart gets its value the way the Claude provider computes it. */
        usage.provider_id = "test";
        usage.as_of = NOW;
        usage.window_count = 1;
        snprintf(usage.windows[0].label, sizeof usage.windows[0].label, "5-hour session");
        usage.windows[0].used_fraction = used_percent / 100.0;
        usage.windows[0].resets_at = FIVE_HOUR_RESET;

        CHECK_INT_EQ(LFCC_OK, statusline_format(&record, NOW, line, sizeof line));
        CHECK_INT_EQ(LFCC_OK, chart_render(&usage, &options, chart, sizeof chart));

        snprintf(expected_line, sizeof expected_line, "5h %d%% left", (int)(100.0 - used_percent + 0.5));
        snprintf(expected_chart, sizeof expected_chart, " %d%% left", (int)(100.0 - used_percent + 0.5));
        CHECK_STR_EQ(expected_line, line);
        CHECK_CONTAINS(chart, expected_chart);
    }
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
    cli_io_t io = {in, out, err, data_dir, now, NULL, {80, false, false}, 0};

    CHECK(fwrite(input, 1, length, in) == length);
    rewind(in);
    result.exit_code = statusline_run(&io);

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
    /* A valid payload followed by more than the limit: only the size cap can reject this. */
    static const char valid[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":10,\"resets_at\":1738425600}}}";
    char data_dir[PATH_CAP];
    size_t size = CLAUDE_INPUT_MAX_BYTES + 100;
    char *huge = malloc(size);
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    CHECK(huge != NULL);
    memset(huge, ' ', size);
    memcpy(huge, valid, sizeof valid - 1);

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
    /* The carried 5-hour window is from the first update, so the record must not claim to be newer. */
    CHECK_INT_EQ(NOW, saved.as_of);
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

static void test_an_unsafe_previous_snapshot_is_replaced_with_a_warning(void)
{
    static const char update[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":20,\"resets_at\":1738425600}}}";
    char data_dir[PATH_CAP];
    char path[PATH_CAP];
    struct stat info;
    snap_record_t saved = {0};
    run_result_t result;

    make_data_dir(data_dir, sizeof data_dir);
    run_text(data_dir, update, NOW);
    snprintf(path, sizeof path, "%s/%s", data_dir, SNAP_FILE_NAME);
    CHECK(chmod(path, 0644) == 0); /* readable by others: the store refuses to trust it */

    result = run_text(data_dir, update, NOW + 60);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("5h 80% left\n", result.out);
    CHECK_CONTAINS(result.err, "ignoring the previous snapshot");
    CHECK(stat(path, &info) == 0);
    CHECK_INT_EQ(0600, info.st_mode & 0777); /* the replacement is private again */
    CHECK_INT_EQ(LFCC_OK, read_snapshot(data_dir, &saved));
    CHECK_INT_EQ(NOW + 60, saved.as_of);

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

static void test_run_from_a_terminal_explains_itself_instead_of_waiting(void)
{
    int master = -1;
    int slave = -1;
    FILE *in = NULL;
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    cli_io_t io = {NULL, out, err, "/nonexistent", NOW, NULL, {80, false, false}, 0};
    char out_text[OUT_CAP];
    char err_text[OUT_CAP];

    CHECK(openpty(&master, &slave, NULL, NULL, NULL) == 0);
    in = fdopen(slave, "r");
    io.in = in;

    CHECK_INT_EQ(2, statusline_run(&io)); /* returns at once: it did not wait for input */

    tk_read_all(out, out_text, sizeof out_text);
    tk_read_all(err, err_text, sizeof err_text);
    CHECK_STR_EQ("", out_text);
    CHECK_CONTAINS(err_text, "run by Claude Code");
    fclose(in);
    close(master);
    fclose(out);
    fclose(err);
}

static void test_an_unwritable_data_directory_still_prints_the_line_and_warns(void)
{
    static const char update[] =
        "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":20,\"resets_at\":1738425600}}}";
    char data_dir[PATH_CAP];
    run_result_t result;

    if (geteuid() == 0) {
        return; /* root ignores directory permissions, so the scenario cannot be created */
    }
    make_data_dir(data_dir, sizeof data_dir);
    CHECK(chmod(data_dir, 0500) == 0); /* private, but nothing can be created in it */

    result = run_text(data_dir, update, NOW);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("5h 80% left\n", result.out);
    CHECK_CONTAINS(result.err, "cannot save the usage snapshot");

    chmod(data_dir, 0700);
    tk_remove_dir(data_dir);
}

int main(void)
{
    RUN_TEST(test_run_from_a_terminal_explains_itself_instead_of_waiting);
    RUN_TEST(test_an_unwritable_data_directory_still_prints_the_line_and_warns);
    RUN_TEST(test_status_line_and_chart_agree_on_the_percentage_left);
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
    RUN_TEST(test_an_unsafe_previous_snapshot_is_replaced_with_a_warning);
    RUN_TEST(test_a_corrupt_previous_snapshot_does_not_stop_the_update);
    RUN_TEST(test_storage_failure_still_prints_the_line_and_warns_on_stderr);
    return TESTKIT_RESULT();
}
