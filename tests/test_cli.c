#include "testkit.h"

#include <unistd.h>

#include "cli/cli.h"
#include "store/snapshot_store.h"
#include "util/secure_path.h"
#include "util/version.h"

#define OUT_CAP 8192
#define PATH_CAP 512
#define FIXTURE_CAP 16384
#define NOW 1738400000
#define NO_DATA_DIR "/nonexistent-lfcc-data"
#define TEST_EXECUTABLE "/opt/homebrew/bin/lf-capcheck"

typedef struct {
    int exit_code;
    char out[OUT_CAP];
    char err[OUT_CAP];
} cli_result_t;

/* Runs cli_run with the given arguments and stdin text, capturing stdout/stderr. */
static cli_result_t run_cli_with(int argc, const char *const argv[], const char *input,
                                 const char *data_dir, const char *executable)
{
    cli_result_t result = {0};
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    cli_io_t io = {in, out, err, data_dir, NOW, executable, {80, false, true}, 0};

    if (input != NULL) {
        fputs(input, in);
        rewind(in);
    }
    result.exit_code = cli_run(argc, argv, &io);
    tk_read_all(out, result.out, sizeof result.out);
    tk_read_all(err, result.err, sizeof result.err);
    fclose(in);
    fclose(out);
    fclose(err);
    return result;
}

static cli_result_t run_cli(int argc, const char *const argv[], const char *input,
                            const char *data_dir)
{
    return run_cli_with(argc, argv, input, data_dir, TEST_EXECUTABLE);
}

static void test_version_flag_prints_name_and_version(void)
{
    const char *const argv[] = {"lf-capcheck", "--version"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("lf-capcheck " LFCC_VERSION "\n", result.out);
    CHECK_STR_EQ("", result.err);
}

static void test_help_flag_prints_usage_including_the_commands(void)
{
    const char *const argv[] = {"lf-capcheck", "--help"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
    CHECK_CONTAINS(result.out, "statusline");
    CHECK_CONTAINS(result.out, "interactive menu");
    CHECK_CONTAINS(result.out, "--watch");
    CHECK_CONTAINS(result.out, "--interval SECONDS");
    CHECK_STR_EQ("", result.err);
}

static void test_short_help_flag_prints_usage(void)
{
    const char *const argv[] = {"lf-capcheck", "-h"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
}

static void test_no_arguments_opens_the_menu_with_claude_first(void)
{
    const char *const argv[] = {"lf-capcheck"};

    cli_result_t result = run_cli(1, argv, "q\n", NO_DATA_DIR);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "AI usage limits");
    CHECK_CONTAINS(result.out, "1) Claude  not connected");
    CHECK_CONTAINS(result.out, "2) Codex   coming soon");
    CHECK_STR_EQ("", result.err);
}

static void test_the_menu_ends_cleanly_when_input_ends(void)
{
    const char *const argv[] = {"lf-capcheck"};

    cli_result_t result = run_cli(1, argv, "", NO_DATA_DIR);

    CHECK_INT_EQ(0, result.exit_code);
}

static void test_connect_snippet_uses_the_running_programs_own_path_by_default(void)
{
    const char *const argv[] = {"lf-capcheck"};

    /* No executable injected: the menu must look up the real path of this test binary. */
    cli_result_t result = run_cli_with(1, argv, "1\nq\n", NO_DATA_DIR, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    /* The path may be shell-quoted (a checkout path containing spaces), so check the parts. */
    CHECK_CONTAINS(result.out, "test_cli");
    CHECK_CONTAINS(result.out, " statusline\"");
    CHECK_CONTAINS(result.out, "\"statusLine\"");
}

static void test_the_menu_shows_a_saved_snapshot_end_to_end(void)
{
    const char *const argv[] = {"lf-capcheck"};
    char data_dir[PATH_CAP];
    snap_record_t record = {0};
    cli_result_t result;
    int fd = -1;

    CHECK(tk_make_temp_dir(data_dir, sizeof data_dir) == 0);
    record.as_of = NOW - 60;
    record.five_hour = (snap_window_t){true, 30.0, NOW + 3600};
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(data_dir, &fd));
    CHECK_INT_EQ(LFCC_OK, snap_write(fd, &record));
    close(fd);

    result = run_cli(1, argv, "1\nq\n", data_dir);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "1) Claude  updated 1 min ago");
    CHECK_CONTAINS(result.out, "70% left");

    tk_remove_dir(data_dir);
}

/* A data directory holding a saved snapshot, for the watch tests. */
static void make_snapshot_dir(char *data_dir, size_t cap)
{
    snap_record_t record = {0};
    int fd = -1;

    CHECK(tk_make_temp_dir(data_dir, cap) == 0);
    record.as_of = NOW - 60;
    record.five_hour = (snap_window_t){true, 30.0, NOW + 3600};
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(data_dir, &fd));
    CHECK_INT_EQ(LFCC_OK, snap_write(fd, &record));
    close(fd);
}

static void test_watch_shows_the_first_provider_and_quits_on_q(void)
{
    const char *const argv[] = {"lf-capcheck", "--watch"};
    char data_dir[PATH_CAP];
    cli_result_t result;

    make_snapshot_dir(data_dir, sizeof data_dir);

    result = run_cli(2, argv, "q\n", data_dir);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Claude - remaining usage");
    CHECK_CONTAINS(result.out, "70% left");
    CHECK_CONTAINS(result.out, "Updating every 5 s.");
    CHECK_STR_EQ("", result.err);
    CHECK(strchr(result.out, '\x1b') == NULL); /* the test output is not a terminal: nothing is cleared */

    tk_remove_dir(data_dir);
}

static void test_watch_accepts_the_short_flag_an_interval_and_a_provider(void)
{
    const char *const argv[] = {"lf-capcheck", "-w", "--interval", "30", "claude"};
    char data_dir[PATH_CAP];
    cli_result_t result;

    make_snapshot_dir(data_dir, sizeof data_dir);

    result = run_cli(5, argv, "q\n", data_dir);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Updating every 30 s.");

    tk_remove_dir(data_dir);
}

static void test_watch_before_connecting_says_so_and_ends_with_the_input(void)
{
    const char *const argv[] = {"lf-capcheck", "--watch"};

    cli_result_t result = run_cli(2, argv, "", NO_DATA_DIR);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Claude is not connected yet.");
}

static void test_watch_on_a_provider_that_is_not_available_fails(void)
{
    const char *const argv[] = {"lf-capcheck", "--watch", "codex"};

    cli_result_t result = run_cli(3, argv, "q\n", NO_DATA_DIR);

    CHECK_INT_EQ(1, result.exit_code);
    CHECK_CONTAINS(result.out, "Codex is not available yet.");
}

static void test_watch_with_an_unknown_provider_lists_the_known_ones(void)
{
    const char *const argv[] = {"lf-capcheck", "--watch", "nope"};

    cli_result_t result = run_cli(3, argv, "q\n", NO_DATA_DIR);

    CHECK_INT_EQ(2, result.exit_code);
    CHECK_STR_EQ("", result.out);
    CHECK_CONTAINS(result.err, "unknown provider 'nope'");
    CHECK_CONTAINS(result.err, "claude, codex");
}

static void test_watch_rejects_bad_arguments_with_a_usage_error(void)
{
    const char *const zero[] = {"lf-capcheck", "--watch", "--interval", "0"};
    const char *const hostile[] = {"lf-capcheck", "--watch", "--bogus\x1b[31m"};
    cli_result_t bad_interval = run_cli(4, zero, "q\n", NO_DATA_DIR);
    cli_result_t bad_option = run_cli(3, hostile, "q\n", NO_DATA_DIR);

    CHECK_INT_EQ(2, bad_interval.exit_code);
    CHECK_CONTAINS(bad_interval.err, "interval");
    CHECK_CONTAINS(bad_interval.err, "--help");
    CHECK_STR_EQ("", bad_interval.out);

    CHECK_INT_EQ(2, bad_option.exit_code);
    CHECK(strchr(bad_option.err, '\x1b') == NULL); /* untrusted text is sanitised */
    CHECK_CONTAINS(bad_option.err, "unknown option '--bogus?[31m'");
}

static void test_unknown_option_fails_with_exit_code_2(void)
{
    const char *const argv[] = {"lf-capcheck", "--bogus"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(2, result.exit_code);
    CHECK_STR_EQ("", result.out);
    CHECK_CONTAINS(result.err, "unknown option '--bogus'");
    CHECK_CONTAINS(result.err, "--help");
}

static void test_terminal_escapes_in_an_unknown_option_are_not_echoed_raw(void)
{
    const char *const argv[] = {"lf-capcheck", "\x1b[31mred\xc2\x9b"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(2, result.exit_code);
    CHECK(strchr(result.err, '\x1b') == NULL);
    CHECK(strstr(result.err, "\xc2\x9b") == NULL);
    CHECK_CONTAINS(result.err, "unknown option '?[31mred?" "?'"); /* split: "??'" is a trigraph */
}

static void test_statusline_command_reads_stdin_saves_and_prints(void)
{
    const char *const argv[] = {"lf-capcheck", "statusline"};
    char json[FIXTURE_CAP];
    char data_dir[PATH_CAP];
    long length = tk_read_file("tests/fixtures/statusline_full.json", json, sizeof json);
    snap_record_t saved = {0};
    cli_result_t result;
    int fd = -1;

    CHECK(length > 0);
    CHECK(tk_make_temp_dir(data_dir, sizeof data_dir) == 0);

    result = run_cli(2, argv, json, data_dir);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("5h 77% left \xc2\xb7 7d 59% left\n", result.out); /* reset times are after NOW */
    CHECK_STR_EQ("", result.err);
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(data_dir, &fd));
    CHECK_INT_EQ(LFCC_OK, snap_read(fd, &saved));
    CHECK_INT_EQ(NOW, saved.as_of);

    close(fd);
    tk_remove_dir(data_dir);
}

int main(void)
{
    RUN_TEST(test_version_flag_prints_name_and_version);
    RUN_TEST(test_help_flag_prints_usage_including_the_commands);
    RUN_TEST(test_short_help_flag_prints_usage);
    RUN_TEST(test_no_arguments_opens_the_menu_with_claude_first);
    RUN_TEST(test_the_menu_ends_cleanly_when_input_ends);
    RUN_TEST(test_connect_snippet_uses_the_running_programs_own_path_by_default);
    RUN_TEST(test_the_menu_shows_a_saved_snapshot_end_to_end);
    RUN_TEST(test_watch_shows_the_first_provider_and_quits_on_q);
    RUN_TEST(test_watch_accepts_the_short_flag_an_interval_and_a_provider);
    RUN_TEST(test_watch_before_connecting_says_so_and_ends_with_the_input);
    RUN_TEST(test_watch_on_a_provider_that_is_not_available_fails);
    RUN_TEST(test_watch_with_an_unknown_provider_lists_the_known_ones);
    RUN_TEST(test_watch_rejects_bad_arguments_with_a_usage_error);
    RUN_TEST(test_unknown_option_fails_with_exit_code_2);
    RUN_TEST(test_terminal_escapes_in_an_unknown_option_are_not_echoed_raw);
    RUN_TEST(test_statusline_command_reads_stdin_saves_and_prints);
    return TESTKIT_RESULT();
}
