#include "testkit.h"

#include <sys/stat.h>
#include <unistd.h>

#include "providers/builtin.h"
#include "store/snapshot_store.h"
#include "ui/menu.h"
#include "util/secure_path.h"

#define NOW 1738411200
#define OUT_CAP 32768
#define PATH_CAP 512
#define EXECUTABLE "/opt/homebrew/bin/lf-capcheck"

/* ---- fake providers ---- */

static int loads = 0; /* how many times any fake was asked for data */

static void fill_usage(usage_snapshot_t *out, bool expired)
{
    usage_snapshot_t usage = {0};

    usage.provider_id = "claude";
    usage.as_of = NOW - 180;
    usage.window_count = 1;
    usage.windows[0] = (usage_window_t){"5-hour session", 0.38, NOW + 8040, expired};
    *out = usage;
}

static lfcc_status_t load_ok(const provider_t *self, const provider_env_t *env, usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    loads++;
    fill_usage(out, false);
    return LFCC_OK;
}

static lfcc_status_t load_all_expired(const provider_t *self, const provider_env_t *env,
                                      usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    loads++;
    fill_usage(out, true);
    return LFCC_OK;
}

/* Data captured 3h 05m before NOW: older than the 30-minute freshness limit. */
static lfcc_status_t load_stale(const provider_t *self, const provider_env_t *env, usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    loads++;
    fill_usage(out, false);
    out->as_of = NOW - (3 * 3600 + 5 * 60);
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

/* Not connected for the first two loads, connected from the third on. */
static lfcc_status_t load_connects_on_third_call(const provider_t *self, const provider_env_t *env,
                                                 usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    loads++;
    if (loads < 3) {
        return LFCC_ERR_NOT_CONNECTED;
    }
    fill_usage(out, false);
    return LFCC_OK;
}

static const provider_t CLAUDE_OK = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_ok};
static const provider_t CLAUDE_STALE = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_stale};
static const provider_t CLAUDE_EXPIRED = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_all_expired};
static const provider_t CLAUDE_NOT_CONNECTED = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_not_connected};
static const provider_t CLAUDE_UNREADABLE = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_unreadable};
static const provider_t CLAUDE_LATE = {"claude", "Claude", PROVIDER_SORT_CLAUDE, load_connects_on_third_call};
static const provider_t CODEX_SOON = {"codex", "Codex", PROVIDER_SORT_CODEX, load_unavailable};
static const provider_t OTHER_NOT_CONNECTED = {"other", "Other", 20, load_not_connected};

/* ---- harness ---- */

typedef struct {
    int exit_code;
    char out[OUT_CAP];
} menu_result_t;

static registry_t registry_of(const provider_t *first, const provider_t *second)
{
    const provider_t *list[2] = {first, second};
    registry_t registry;

    CHECK_INT_EQ(LFCC_OK, registry_build(list, second != NULL ? 2 : 1, &registry));
    return registry;
}

static menu_env_t env_with(FILE *in, FILE *out, const char *data_dir, terminal_style_t style)
{
    return (menu_env_t){in, out, data_dir, EXECUTABLE, NOW, style};
}

static menu_result_t run_full(const registry_t *registry, const char *input, const char *data_dir,
                              terminal_style_t style)
{
    menu_result_t result = {0};
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    menu_env_t env = env_with(in, out, data_dir, style);

    fputs(input, in);
    rewind(in);
    loads = 0;
    result.exit_code = menu_run(registry, &env);
    tk_read_all(out, result.out, sizeof result.out);
    fclose(in);
    fclose(out);
    return result;
}

static menu_result_t run(const registry_t *registry, const char *input)
{
    return run_full(registry, input, "/nonexistent-lfcc-data", (terminal_style_t){80, false, true});
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

/* ---- main page ---- */

static void test_main_page_lists_providers_in_order_with_their_state(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, &CODEX_SOON);
    menu_result_t result = run(&registry, "q\n");

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "AI usage limits");
    CHECK_CONTAINS(result.out, "1) Claude  updated 3 min ago");
    CHECK_CONTAINS(result.out, "2) Codex   coming soon");
    CHECK(strstr(result.out, "1) Claude") < strstr(result.out, "2) Codex"));
    CHECK_CONTAINS(result.out, "Choose a provider (1-2) or q to quit: ");
}

static void test_main_page_describes_every_kind_of_state(void)
{
    registry_t not_connected = registry_of(&CLAUDE_NOT_CONNECTED, NULL);
    registry_t expired = registry_of(&CLAUDE_EXPIRED, NULL);
    registry_t unreadable = registry_of(&CLAUDE_UNREADABLE, NULL);
    menu_result_t not_connected_page = run(&not_connected, "q\n");
    menu_result_t expired_page = run(&expired, "q\n");
    menu_result_t unreadable_page = run(&unreadable, "q\n");

    CHECK_CONTAINS(not_connected_page.out, "1) Claude  not connected");
    CHECK_CONTAINS(expired_page.out, "1) Claude  limits reset, waiting for activity");
    CHECK_CONTAINS(unreadable_page.out, "1) Claude  saved data cannot be read");
    CHECK_CONTAINS(not_connected_page.out, "Choose a provider (1) or q to quit: ");
}

static void test_old_data_is_flagged_on_the_main_page_and_on_the_usage_screen(void)
{
    registry_t registry = registry_of(&CLAUDE_STALE, NULL);
    menu_result_t result = run(&registry, "1\nq\n");

    CHECK_CONTAINS(result.out, "1) Claude  updated 3h 05m ago, may be out of date");
    CHECK_CONTAINS(result.out, "Data may be out of date. Claude Code reports your limits while it runs");
}

static void test_fresh_data_carries_no_staleness_notes(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t result = run(&registry, "1\nq\n");

    CHECK(strstr(result.out, "out of date") == NULL);
}

/* ---- usage screen ---- */

static void test_choosing_a_connected_provider_shows_the_chart_and_back_returns(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, &CODEX_SOON);
    menu_result_t result = run(&registry, "1\nb\nq\n");

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Claude - remaining usage");
    CHECK_CONTAINS(result.out, "5-hour session");
    CHECK_CONTAINS(result.out, "62% left");
    CHECK_CONTAINS(result.out, "[r] reload");
    CHECK_INT_EQ(2, count(result.out, "AI usage limits")); /* main page shown again after back */
}

static void test_reload_reads_the_data_again(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t result = run(&registry, "1\nr\nr\nb\nq\n");

    CHECK_INT_EQ(3, count(result.out, "Claude - remaining usage")); /* opened + two reloads */
}

static void test_quit_works_from_the_usage_screen_without_returning_to_the_main_page(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t result = run(&registry, "1\nq\n");

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(1, count(result.out, "AI usage limits"));
}

static void test_keys_are_not_case_sensitive(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t result = run(&registry, "1\nR\nB\nQ\n");

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(2, count(result.out, "Claude - remaining usage"));
    CHECK_INT_EQ(2, count(result.out, "AI usage limits"));
}

static void test_unknown_keys_on_the_usage_screen_show_a_hint(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t result = run(&registry, "1\nx\n\nrr\nq\n");

    CHECK_INT_EQ(3, count(result.out, "Press r to reload, b to go back or q to quit."));
}

static void test_style_reaches_the_chart(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t fancy = run_full(&registry, "1\nq\n", "/x", (terminal_style_t){80, true, true});
    menu_result_t plain = run_full(&registry, "1\nq\n", "/x", (terminal_style_t){80, false, false});

    CHECK(strchr(fancy.out, '\x1b') != NULL);
    CHECK_CONTAINS(fancy.out, "\xe2\x96\x88");
    CHECK(strchr(plain.out, '\x1b') == NULL);
    CHECK_CONTAINS(plain.out, "#");
    CHECK(strstr(plain.out, "\xe2\x96\x88") == NULL);
}

/* ---- connect screen ---- */

static void test_an_unconnected_claude_shows_how_to_connect_with_the_exact_snippet(void)
{
    registry_t registry = registry_of(&CLAUDE_NOT_CONNECTED, NULL);
    menu_result_t result = run(&registry, "1\nb\nq\n");

    CHECK_CONTAINS(result.out, "Claude is not connected yet.");
    CHECK_CONTAINS(result.out, "~/.claude/settings.json");
    CHECK_CONTAINS(result.out, "\"statusLine\": {");
    CHECK_CONTAINS(result.out, "\"command\": \"" EXECUTABLE " statusline\"");
    CHECK_CONTAINS(result.out, "Send a message in Claude Code");
    CHECK_CONTAINS(result.out, "[r] check again");
    CHECK_INT_EQ(2, count(result.out, "AI usage limits"));
}

static void test_check_again_stays_put_while_nothing_has_arrived(void)
{
    registry_t registry = registry_of(&CLAUDE_NOT_CONNECTED, NULL);
    menu_result_t result = run(&registry, "1\nr\nr\nq\n");

    CHECK_INT_EQ(3, count(result.out, "\"statusLine\": {")); /* opened + two re-checks */
    CHECK(strstr(result.out, "remaining usage") == NULL);
}

static void test_check_again_moves_on_to_the_chart_once_data_arrives(void)
{
    /* Loads: main page (1), opening Claude (2), then "r" (3) which succeeds. */
    registry_t registry = registry_of(&CLAUDE_LATE, NULL);
    menu_result_t result = run(&registry, "1\nr\nq\n");

    CHECK_INT_EQ(1, count(result.out, "\"statusLine\": {"));
    CHECK_CONTAINS(result.out, "Claude - remaining usage");
    CHECK_CONTAINS(result.out, "62% left");
}

static void test_a_provider_that_is_not_claude_gets_a_generic_message(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, &OTHER_NOT_CONNECTED);
    menu_result_t result = run(&registry, "2\nq\n");

    CHECK_CONTAINS(result.out, "Other is not connected.");
    CHECK(strstr(result.out, "statusLine") == NULL);
}

/* ---- other outcomes ---- */

static void test_an_unavailable_provider_says_it_is_coming_soon_and_returns(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, &CODEX_SOON);
    menu_result_t result = run(&registry, "2\nq\n");

    CHECK_CONTAINS(result.out, "Codex is not available yet.");
    CHECK_INT_EQ(2, count(result.out, "AI usage limits"));
}

static void test_unreadable_data_shows_a_friendly_error_and_returns(void)
{
    registry_t registry = registry_of(&CLAUDE_UNREADABLE, NULL);
    menu_result_t result = run(&registry, "1\nq\n");

    CHECK_CONTAINS(result.out, "Cannot show Claude: data is malformed or out of range.");
    CHECK_INT_EQ(2, count(result.out, "AI usage limits"));
}

/* ---- input handling ---- */

static void test_invalid_choices_are_rejected_and_asked_again(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, &CODEX_SOON);
    menu_result_t result = run(&registry, "abc\n0\n3\n-1\n1x\n\n   \n1 \nb\nq\n");

    CHECK_INT_EQ(7, count(result.out, "Please enter a number from 1 to 2, or q."));
    CHECK_INT_EQ(1, count(result.out, "Claude - remaining usage")); /* "1 " with a trailing space is valid */
}

static void test_an_overlong_line_is_one_invalid_answer_not_several(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    char input[256];

    memset(input, 'x', 70);
    strcpy(input + 70, "\nq\n");
    menu_result_t result = run(&registry, input);

    CHECK_INT_EQ(1, count(result.out, "Please enter a number"));
    CHECK_INT_EQ(0, result.exit_code);
}

static void test_end_of_input_ends_the_menu_cleanly(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);

    CHECK_INT_EQ(0, run(&registry, "").exit_code);         /* at the main page */
    CHECK_INT_EQ(0, run(&registry, "1\n").exit_code);      /* on the usage screen */
    CHECK_INT_EQ(0, run(&registry, "1\nr").exit_code);     /* last line without a newline */
    registry_t offline = registry_of(&CLAUDE_NOT_CONNECTED, NULL);
    CHECK_INT_EQ(0, run(&offline, "1\n").exit_code);       /* on the connect screen */
}

static void test_a_last_line_without_a_newline_still_counts(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t result = run(&registry, "1\nb\nq");

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(2, count(result.out, "AI usage limits"));
}

static void test_windows_line_endings_are_accepted(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    menu_result_t result = run(&registry, "1\r\nb\r\nq\r\n");

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_INT_EQ(1, count(result.out, "Claude - remaining usage"));
}

static void test_rejects_missing_arguments(void)
{
    registry_t registry = registry_of(&CLAUDE_OK, NULL);
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    menu_env_t good = env_with(in, out, "/x", (terminal_style_t){80, false, true});
    menu_env_t no_in = good;
    menu_env_t no_out = good;
    menu_env_t no_dir = good;
    menu_env_t no_exe = good;

    no_in.in = NULL;
    no_out.out = NULL;
    no_dir.data_dir = NULL;
    no_exe.executable = NULL;

    CHECK_INT_EQ(1, menu_run(NULL, &good));
    CHECK_INT_EQ(1, menu_run(&registry, NULL));
    CHECK_INT_EQ(1, menu_run(&registry, &no_in));
    CHECK_INT_EQ(1, menu_run(&registry, &no_out));
    CHECK_INT_EQ(1, menu_run(&registry, &no_dir));
    CHECK_INT_EQ(1, menu_run(&registry, &no_exe));
    fclose(in);
    fclose(out);
}

/* ---- with the real providers and a real data directory ---- */

static void test_end_to_end_with_the_real_claude_provider(void)
{
    registry_t registry;
    char dir[PATH_CAP];
    snap_record_t record = {0};
    int fd = -1;
    menu_result_t result;

    CHECK_INT_EQ(LFCC_OK, builtin_registry_build(&registry));
    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    record.as_of = NOW - 180;
    record.five_hour = (snap_window_t){true, 38.0, NOW + 8040};
    record.seven_day = (snap_window_t){true, 79.0, NOW + 3 * 86400 + 4 * 3600};
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(dir, &fd));
    CHECK_INT_EQ(LFCC_OK, snap_write(fd, &record));
    close(fd);

    result = run_full(&registry, "1\nb\nq\n", dir, (terminal_style_t){80, false, true});

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "1) Claude  updated 3 min ago");
    CHECK_CONTAINS(result.out, "2) Codex   coming soon");
    CHECK_CONTAINS(result.out, "5-hour session");
    CHECK_CONTAINS(result.out, "62% left");
    CHECK_CONTAINS(result.out, "Weekly");
    CHECK_CONTAINS(result.out, "21% left");
    CHECK_CONTAINS(result.out, "resets in 3d 4h");

    tk_remove_dir(dir);
}

static void test_end_to_end_without_a_snapshot_offers_to_connect_and_creates_nothing(void)
{
    registry_t registry;
    char dir[PATH_CAP];
    char missing[PATH_CAP + 16];
    struct stat info;
    menu_result_t result;

    CHECK_INT_EQ(LFCC_OK, builtin_registry_build(&registry));
    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    snprintf(missing, sizeof missing, "%s/not-created", dir);

    result = run_full(&registry, "1\nq\n", missing, (terminal_style_t){80, false, true});

    CHECK_CONTAINS(result.out, "1) Claude  not connected");
    CHECK_CONTAINS(result.out, "Claude is not connected yet.");
    CHECK_CONTAINS(result.out, "\"command\": \"" EXECUTABLE " statusline\"");
    CHECK(stat(missing, &info) != 0); /* browsing the menu must not create the data directory */

    tk_remove_dir(dir);
}

int main(void)
{
    RUN_TEST(test_main_page_lists_providers_in_order_with_their_state);
    RUN_TEST(test_main_page_describes_every_kind_of_state);
    RUN_TEST(test_old_data_is_flagged_on_the_main_page_and_on_the_usage_screen);
    RUN_TEST(test_fresh_data_carries_no_staleness_notes);
    RUN_TEST(test_choosing_a_connected_provider_shows_the_chart_and_back_returns);
    RUN_TEST(test_reload_reads_the_data_again);
    RUN_TEST(test_quit_works_from_the_usage_screen_without_returning_to_the_main_page);
    RUN_TEST(test_keys_are_not_case_sensitive);
    RUN_TEST(test_unknown_keys_on_the_usage_screen_show_a_hint);
    RUN_TEST(test_style_reaches_the_chart);
    RUN_TEST(test_an_unconnected_claude_shows_how_to_connect_with_the_exact_snippet);
    RUN_TEST(test_check_again_stays_put_while_nothing_has_arrived);
    RUN_TEST(test_check_again_moves_on_to_the_chart_once_data_arrives);
    RUN_TEST(test_a_provider_that_is_not_claude_gets_a_generic_message);
    RUN_TEST(test_an_unavailable_provider_says_it_is_coming_soon_and_returns);
    RUN_TEST(test_unreadable_data_shows_a_friendly_error_and_returns);
    RUN_TEST(test_invalid_choices_are_rejected_and_asked_again);
    RUN_TEST(test_an_overlong_line_is_one_invalid_answer_not_several);
    RUN_TEST(test_end_of_input_ends_the_menu_cleanly);
    RUN_TEST(test_a_last_line_without_a_newline_still_counts);
    RUN_TEST(test_windows_line_endings_are_accepted);
    RUN_TEST(test_rejects_missing_arguments);
    RUN_TEST(test_end_to_end_with_the_real_claude_provider);
    RUN_TEST(test_end_to_end_without_a_snapshot_offers_to_connect_and_creates_nothing);
    return TESTKIT_RESULT();
}
