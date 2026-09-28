#include "testkit.h"

#include <time.h>
#include <unistd.h>

#include "providers/claude.h"
#include "store/snapshot_store.h"
#include "ui/watch.h"
#include "util/secure_path.h"

#define NOW 1738411200
#define OUT_CAP 32768
#define PATH_CAP 512
#define CLEAR "\x1b[H\x1b[2J"

/* ---- argument parsing ---- */

static lfcc_status_t parse(int argc, const char *const argv[], watch_options_t *options, char *error)
{
    return watch_parse_args(argc, argv, options, error, 128);
}

static void test_defaults_are_a_five_second_interval_and_the_first_provider(void)
{
    watch_options_t options = {0};
    char error[128];

    CHECK_INT_EQ(LFCC_OK, parse(0, NULL, &options, error));

    CHECK_INT_EQ(WATCH_DEFAULT_INTERVAL_SECONDS, options.interval_seconds);
    CHECK(options.provider_id == NULL);
}

static void test_reads_an_interval_and_a_provider_in_either_order(void)
{
    const char *const interval_first[] = {"--interval", "30", "codex"};
    const char *const provider_first[] = {"claude", "--interval", "1"};
    const char *const only_provider[] = {"claude"};
    watch_options_t options = {0};
    char error[128];

    CHECK_INT_EQ(LFCC_OK, parse(3, interval_first, &options, error));
    CHECK_INT_EQ(30, options.interval_seconds);
    CHECK_STR_EQ("codex", options.provider_id);

    CHECK_INT_EQ(LFCC_OK, parse(3, provider_first, &options, error));
    CHECK_INT_EQ(1, options.interval_seconds);
    CHECK_STR_EQ("claude", options.provider_id);

    CHECK_INT_EQ(LFCC_OK, parse(1, only_provider, &options, error));
    CHECK_INT_EQ(WATCH_DEFAULT_INTERVAL_SECONDS, options.interval_seconds);
}

static void test_accepts_the_interval_limits(void)
{
    const char *const smallest[] = {"--interval", "1"};
    const char *const largest[] = {"--interval", "3600"};
    watch_options_t options = {0};
    char error[128];

    CHECK_INT_EQ(LFCC_OK, parse(2, smallest, &options, error));
    CHECK_INT_EQ(1, options.interval_seconds);
    CHECK_INT_EQ(LFCC_OK, parse(2, largest, &options, error));
    CHECK_INT_EQ(WATCH_MAX_INTERVAL_SECONDS, options.interval_seconds);
}

static void test_rejects_bad_intervals_with_a_message(void)
{
    static const char *const bad[] = {"0", "3601", "-5", "+5", "abc", "1x", "", " 5", "5 ",
                                      "99999999999999999999", "1.5"};

    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const char *const args[] = {"--interval", bad[i]};
        watch_options_t options = {0};
        char error[128] = "";

        CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, parse(2, args, &options, error));
        CHECK_CONTAINS(error, "interval");
    }
}

static void test_rejects_a_missing_interval_value_repeats_extra_and_unknown_arguments(void)
{
    const char *const missing[] = {"--interval"};
    const char *const repeated[] = {"--interval", "5", "--interval", "6"};
    const char *const two_providers[] = {"claude", "codex"};
    const char *const unknown[] = {"--bogus"};
    const char *const dash[] = {"-x"};
    watch_options_t options = {0};
    char error[128];

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, parse(1, missing, &options, error));
    CHECK_CONTAINS(error, "interval");
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, parse(4, repeated, &options, error));
    CHECK_CONTAINS(error, "more than once");
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, parse(2, two_providers, &options, error));
    CHECK_CONTAINS(error, "one provider");
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, parse(1, unknown, &options, error));
    CHECK_CONTAINS(error, "unknown option '--bogus'");
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, parse(1, dash, &options, error));
}

static void test_rejects_missing_output_arguments(void)
{
    const char *const args[] = {"claude"};
    watch_options_t options = {0};
    char error[128];

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, watch_parse_args(1, args, NULL, error, sizeof error));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, watch_parse_args(1, args, &options, NULL, 0));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, watch_parse_args(1, NULL, &options, error, sizeof error));
}

/* ---- the loop, with fake providers ---- */

static int loads = 0;

static void fill_usage(usage_snapshot_t *out)
{
    usage_snapshot_t usage = {0};

    usage.provider_id = "claude";
    usage.as_of = NOW - 180;
    usage.window_count = 1;
    usage.windows[0] = (usage_window_t){"5-hour session", 0.38, NOW + 8040, false};
    *out = usage;
}

static lfcc_status_t load_ok(const provider_t *self, const provider_env_t *env, usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    loads++;
    fill_usage(out);
    return LFCC_OK;
}

static lfcc_status_t load_not_connected(const provider_t *self, const provider_env_t *env,
                                        usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    (void)out;
    loads++;
    return LFCC_ERR_NOT_CONNECTED;
}

static lfcc_status_t load_unavailable(const provider_t *self, const provider_env_t *env,
                                      usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    (void)out;
    loads++;
    return LFCC_ERR_UNAVAILABLE;
}

static lfcc_status_t load_unreadable(const provider_t *self, const provider_env_t *env,
                                     usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    (void)out;
    loads++;
    return LFCC_ERR_PARSE;
}

/* Not connected on the first load, connected afterwards. */
static lfcc_status_t load_connects_after_first(const provider_t *self, const provider_env_t *env,
                                               usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    loads++;
    if (loads < 2) {
        return LFCC_ERR_NOT_CONNECTED;
    }
    fill_usage(out);
    return LFCC_OK;
}

static const provider_t CLAUDE_OK = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_ok};
static const provider_t CLAUDE_NOT_CONNECTED = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_not_connected};
static const provider_t CLAUDE_UNREADABLE = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_unreadable};
static const provider_t CLAUDE_LATE = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_connects_after_first};
static const provider_t CODEX_SOON = {"codex", "Codex", PROVIDER_SORT_CODEX, load_unavailable};

typedef struct {
    int exit_code;
    char out[OUT_CAP];
} watch_result_t;

static watch_result_t run_with(const provider_t *provider, FILE *in, watch_options_t options)
{
    watch_result_t result = {0};
    FILE *out = tmpfile();
    menu_env_t env = {in, out, "/nonexistent-lfcc-data", "/opt/homebrew/bin/lf-capcheck", NOW,
                      {80, false, true}};

    loads = 0;
    result.exit_code = watch_run(provider, &env, &options);
    tk_read_all(out, result.out, sizeof result.out);
    fclose(out);
    return result;
}

static watch_options_t default_options(void)
{
    return (watch_options_t){WATCH_DEFAULT_INTERVAL_SECONDS, NULL, false, 0};
}

static watch_result_t run_text(const provider_t *provider, const char *input, watch_options_t options)
{
    FILE *in = tmpfile();
    watch_result_t result;

    fputs(input, in);
    rewind(in);
    result = run_with(provider, in, options);
    fclose(in);
    return result;
}

static int count(const char *text, const char *piece)
{
    int found = 0;
    size_t length = strlen(piece);

    for (const char *at = strstr(text, piece); at != NULL; at = strstr(at + length, piece)) {
        found++;
    }
    return found;
}

static void test_draws_the_chart_and_the_footer_then_quits_on_q(void)
{
    watch_result_t result = run_text(&CLAUDE_OK, "q\n", default_options());

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(1, count(result.out, "Claude - remaining usage"));
    CHECK_CONTAINS(result.out, "62% left");
    CHECK_CONTAINS(result.out, "Updating every 5 s.");
    CHECK_CONTAINS(result.out, "type q and press Enter to quit");
    CHECK_INT_EQ(1, loads);
}

static void test_the_footer_shows_the_chosen_interval(void)
{
    watch_options_t options = default_options();
    watch_result_t result;

    options.interval_seconds = 30;
    result = run_text(&CLAUDE_OK, "q\n", options);

    CHECK_CONTAINS(result.out, "Updating every 30 s.");
}

static void test_enter_refreshes_immediately(void)
{
    watch_result_t result = run_text(&CLAUDE_OK, "\n\nq\n", default_options());

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(3, count(result.out, "Claude - remaining usage")); /* first frame + two refreshes */
    CHECK_INT_EQ(3, loads);
}

static void test_any_other_line_also_refreshes_and_q_is_case_and_spacing_tolerant(void)
{
    watch_result_t refresh = run_text(&CLAUDE_OK, "hello\nq\n", default_options());
    watch_result_t spaced = run_text(&CLAUDE_OK, "\n  Q\n", default_options());
    watch_result_t word = run_text(&CLAUDE_OK, "quit\n", default_options());

    CHECK_INT_EQ(2, count(refresh.out, "Claude - remaining usage"));
    CHECK_INT_EQ(2, count(spaced.out, "Claude - remaining usage"));
    CHECK_INT_EQ(1, count(word.out, "Claude - remaining usage"));
}

static void test_end_of_input_quits_after_the_first_frame(void)
{
    watch_result_t result = run_text(&CLAUDE_OK, "", default_options());

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(1, count(result.out, "Claude - remaining usage"));
}

static void test_redraws_by_itself_when_the_interval_passes(void)
{
    int pipe_fds[2];
    watch_options_t options = default_options();
    struct timespec start;
    struct timespec end;
    watch_result_t result;
    FILE *in = NULL;
    double seconds = 0.0;

    CHECK(pipe(pipe_fds) == 0);
    in = fdopen(pipe_fds[0], "r"); /* the write end stays open and silent */
    options.interval_seconds = 1;
    options.max_frames = 2;

    clock_gettime(CLOCK_MONOTONIC, &start);
    result = run_with(&CLAUDE_OK, in, options);
    clock_gettime(CLOCK_MONOTONIC, &end);
    seconds = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(2, count(result.out, "Claude - remaining usage"));
    CHECK(seconds >= 0.9); /* it waited one interval for the second frame ... */
    CHECK(seconds < 4.0);  /* ... and did not hang */

    fclose(in);
    close(pipe_fds[1]);
}

static void test_the_screen_is_cleared_before_each_frame_only_when_asked(void)
{
    watch_options_t clearing = default_options();
    watch_result_t cleared;
    watch_result_t plain = run_text(&CLAUDE_OK, "\nq\n", default_options());

    clearing.clear_screen = true;
    cleared = run_text(&CLAUDE_OK, "\nq\n", clearing);

    CHECK_INT_EQ(2, count(cleared.out, CLEAR));
    CHECK(cleared.out == strstr(cleared.out, CLEAR)); /* the very first thing written */
    CHECK(strchr(plain.out, '\x1b') == NULL);
}

static void test_not_connected_shows_a_short_pointer_and_keeps_watching(void)
{
    watch_result_t result = run_text(&CLAUDE_NOT_CONNECTED, "\n\nq\n", default_options());

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(3, count(result.out, "Claude is not connected yet."));
    CHECK_CONTAINS(result.out, "Run lf-capcheck and choose Claude");
    CHECK(strstr(result.out, "remaining usage") == NULL);
    CHECK(strstr(result.out, "statusLine") == NULL); /* the full snippet stays in the menu */
}

static void test_picks_up_data_that_arrives_while_watching(void)
{
    watch_result_t result = run_text(&CLAUDE_LATE, "\nq\n", default_options());

    CHECK_INT_EQ(1, count(result.out, "Claude is not connected yet."));
    CHECK_INT_EQ(1, count(result.out, "Claude - remaining usage"));
}

static void test_unreadable_data_is_reported_and_watching_continues(void)
{
    watch_result_t result = run_text(&CLAUDE_UNREADABLE, "\nq\n", default_options());

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(2, count(result.out, "Cannot show Claude: data is malformed or out of range."));
}

static void test_an_unavailable_provider_stops_at_once_with_an_error_code(void)
{
    watch_result_t result = run_text(&CODEX_SOON, "\n\nq\n", default_options());

    CHECK_INT_EQ(1, result.exit_code);
    CHECK_INT_EQ(1, count(result.out, "Codex is not available yet."));
    CHECK_INT_EQ(1, loads);
}

static void test_rejects_missing_or_invalid_arguments(void)
{
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    menu_env_t good = {in, out, "/x", "/opt/lf", NOW, {80, false, true}};
    menu_env_t no_in = good;
    menu_env_t no_dir = good;
    watch_options_t options = default_options();
    watch_options_t zero_interval = default_options();

    no_in.in = NULL;
    no_dir.data_dir = NULL;
    zero_interval.interval_seconds = 0;

    CHECK_INT_EQ(1, watch_run(NULL, &good, &options));
    CHECK_INT_EQ(1, watch_run(&CLAUDE_OK, NULL, &options));
    CHECK_INT_EQ(1, watch_run(&CLAUDE_OK, &good, NULL));
    CHECK_INT_EQ(1, watch_run(&CLAUDE_OK, &no_in, &options));
    CHECK_INT_EQ(1, watch_run(&CLAUDE_OK, &no_dir, &options));
    CHECK_INT_EQ(1, watch_run(&CLAUDE_OK, &good, &zero_interval));
    fclose(in);
    fclose(out);
}

static void test_with_the_real_claude_provider_and_a_saved_snapshot(void)
{
    char dir[PATH_CAP];
    snap_record_t record = {0};
    int fd = -1;
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    menu_env_t env = {in, out, dir, "/opt/homebrew/bin/lf-capcheck", NOW, {80, false, true}};
    watch_options_t options = default_options();
    char text[OUT_CAP];

    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    record.as_of = NOW - 60;
    record.five_hour = (snap_window_t){true, 38.0, NOW + 8040};
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(dir, &fd));
    CHECK_INT_EQ(LFCC_OK, snap_write(fd, &record));
    close(fd);
    fputs("q\n", in);
    rewind(in);

    CHECK_INT_EQ(0, watch_run(claude_provider(), &env, &options));

    tk_read_all(out, text, sizeof text);
    CHECK_CONTAINS(text, "5-hour session");
    CHECK_CONTAINS(text, "62% left");
    fclose(in);
    fclose(out);
    tk_remove_dir(dir);
}

int main(void)
{
    RUN_TEST(test_defaults_are_a_five_second_interval_and_the_first_provider);
    RUN_TEST(test_reads_an_interval_and_a_provider_in_either_order);
    RUN_TEST(test_accepts_the_interval_limits);
    RUN_TEST(test_rejects_bad_intervals_with_a_message);
    RUN_TEST(test_rejects_a_missing_interval_value_repeats_extra_and_unknown_arguments);
    RUN_TEST(test_rejects_missing_output_arguments);
    RUN_TEST(test_draws_the_chart_and_the_footer_then_quits_on_q);
    RUN_TEST(test_the_footer_shows_the_chosen_interval);
    RUN_TEST(test_enter_refreshes_immediately);
    RUN_TEST(test_any_other_line_also_refreshes_and_q_is_case_and_spacing_tolerant);
    RUN_TEST(test_end_of_input_quits_after_the_first_frame);
    RUN_TEST(test_redraws_by_itself_when_the_interval_passes);
    RUN_TEST(test_the_screen_is_cleared_before_each_frame_only_when_asked);
    RUN_TEST(test_not_connected_shows_a_short_pointer_and_keeps_watching);
    RUN_TEST(test_picks_up_data_that_arrives_while_watching);
    RUN_TEST(test_unreadable_data_is_reported_and_watching_continues);
    RUN_TEST(test_an_unavailable_provider_stops_at_once_with_an_error_code);
    RUN_TEST(test_rejects_missing_or_invalid_arguments);
    RUN_TEST(test_with_the_real_claude_provider_and_a_saved_snapshot);
    return TESTKIT_RESULT();
}
