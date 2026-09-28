#include "testkit.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "ui/chart.h"

#define NOW 1738411200 /* 2025-02-01 12:00:00 UTC */
#define OUT_CAP 2048
#define FULL "\xe2\x96\x88"  /* U+2588 */
#define EMPTY "\xe2\x96\x91" /* U+2591 */
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define RED "\x1b[31m"
#define RESET_COLOR "\x1b[0m"

static usage_snapshot_t base_snapshot(void)
{
    usage_snapshot_t snapshot = {0};

    snapshot.provider_id = "claude";
    snapshot.as_of = NOW - 180;
    snapshot.window_count = 2;
    snapshot.windows[0] = (usage_window_t){"5-hour session", 0.38, NOW + 2 * 3600 + 14 * 60, false};
    snapshot.windows[1] = (usage_window_t){"Weekly", 0.79, NOW + 3 * 86400 + 4 * 3600, false};
    return snapshot;
}

static usage_snapshot_t single(double used_fraction, time_t resets_at, bool expired)
{
    usage_snapshot_t snapshot = base_snapshot();

    snapshot.window_count = 1;
    snapshot.windows[0] = (usage_window_t){"Weekly", used_fraction, resets_at, expired};
    return snapshot;
}

static chart_options_t options(int width, bool color, bool unicode)
{
    return (chart_options_t){width, color, unicode, NOW, "Claude"};
}

static void repeat(char *out, size_t cap, const char *piece, int times)
{
    size_t used = 0;

    out[0] = '\0';
    for (int i = 0; i < times; i++) {
        used += (size_t)snprintf(out + used, cap - used, "%s", piece);
    }
}

/* "<filled><empty>" bar text as the renderer should draw it. */
static void bar(char *out, size_t cap, int filled, int empty, bool unicode)
{
    char first[256];
    char second[256];

    repeat(first, sizeof first, unicode ? FULL : "#", filled);
    repeat(second, sizeof second, unicode ? EMPTY : "-", empty);
    snprintf(out, cap, "%s%s", first, second);
}

static void render(const usage_snapshot_t *snapshot, const chart_options_t *opts, char *out)
{
    CHECK_INT_EQ(LFCC_OK, chart_render(snapshot, opts, out, OUT_CAP));
}

static int count_occurrences(const char *text, const char *piece)
{
    int count = 0;
    size_t length = strlen(piece);

    for (const char *at = strstr(text, piece); at != NULL; at = strstr(at + length, piece)) {
        count++;
    }
    return count;
}

static void test_renders_header_and_one_row_per_window(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];
    char expected[OUT_CAP];
    char bar_a[256];
    char bar_b[256];

    bar(bar_a, sizeof bar_a, 12, 8, true);
    bar(bar_b, sizeof bar_b, 4, 16, true);
    snprintf(expected, sizeof expected,
             "Claude - remaining usage                 as of 11:57 (3 min ago)\n"
             "5-hour session %s  62%% left   resets in 2h 14m\n"
             "Weekly         %s  21%% left   resets in 3d 4h\n",
             bar_a, bar_b);

    render(&snapshot, &opts, out);

    CHECK_STR_EQ(expected, out);
}

static void test_ascii_fallback_uses_plain_characters(void)
{
    usage_snapshot_t snapshot = single(0.38, NOW + 600, false);
    chart_options_t opts = options(64, false, false);
    char out[OUT_CAP];
    char expected_bar[256];

    /* label 6 -> bar is 64 - 6 - 30 = 28 cells; 62% left -> 17 filled */
    bar(expected_bar, sizeof expected_bar, 17, 11, false);
    render(&snapshot, &opts, out);

    CHECK(strstr(out, FULL) == NULL && strstr(out, EMPTY) == NULL);
    CHECK_CONTAINS(out, expected_bar);
}

static void test_bar_is_coloured_by_how_much_is_left(void)
{
    static const struct {
        double used;
        const char *colour;
        const char *name;
    } cases[] = {
        {0.00, GREEN, "100% left"}, {0.49, GREEN, "51% left"},  {0.50, YELLOW, "50% left"},
        {0.60, YELLOW, "40% left"}, {0.80, YELLOW, "20% left"}, {0.81, RED, "19% left"},
        {0.95, RED, "5% left"},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        usage_snapshot_t snapshot = single(cases[i].used, NOW + 600, false);
        chart_options_t opts = options(64, true, true);
        char out[OUT_CAP];

        render(&snapshot, &opts, out);

        CHECK_CONTAINS(out, cases[i].name);
        CHECK_CONTAINS(out, cases[i].colour);
        CHECK_CONTAINS(out, RESET_COLOR);
        CHECK_INT_EQ(1, count_occurrences(out, "\x1b[3")); /* exactly one colour, on the bar */
    }
}

static void test_no_escape_codes_without_colour(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];

    render(&snapshot, &opts, out);

    CHECK(strchr(out, '\x1b') == NULL);
}

static void test_bar_is_never_falsely_empty_or_full(void)
{
    static const struct {
        double used;
        int filled;
    } cases[] = {
        {0.00, 30},  /* 100% left: full */
        {1.00, 0},   /* 0% left: empty */
        {0.991, 1},  /* 1% left: still shows one cell */
        {0.99, 1},
        {0.011, 29}, /* 99% left: not drawn as completely full */
        {0.996, 0},  /* rounds to 0% left: empty */
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        usage_snapshot_t snapshot = single(cases[i].used, NOW + 600, false);
        chart_options_t opts = options(80, false, true);
        char out[OUT_CAP];

        render(&snapshot, &opts, out);

        CHECK_INT_EQ(cases[i].filled, count_occurrences(out, FULL));
        CHECK_INT_EQ(30 - cases[i].filled, count_occurrences(out, EMPTY));
    }
}

static void test_bar_width_follows_the_terminal_within_limits(void)
{
    static const struct {
        int width;
        int bar_cells;
    } cases[] = {
        {0, 30},   /* unknown width: 80 columns, bar capped at 30 */
        {40, 10},  /* narrow: bar never below 10 */
        {5, 10},
        {66, 30},  /* 66 - 6 (label) - 30 = 30 */
        {65, 29},
        {200, 30}, /* wide: bar never above 30 */
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        usage_snapshot_t snapshot = single(0.50, NOW + 600, false);
        chart_options_t opts = options(cases[i].width, false, true);
        char out[OUT_CAP];

        render(&snapshot, &opts, out);

        CHECK_INT_EQ(cases[i].bar_cells, count_occurrences(out, FULL) + count_occurrences(out, EMPTY));
    }
}

static void test_a_very_wide_terminal_still_draws_the_chart(void)
{
    static const int widths[] = {201, 2000, 5000, 65535, INT_MAX};

    for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++) {
        usage_snapshot_t snapshot = base_snapshot();
        chart_options_t opts = options(widths[i], false, true);
        char out[OUT_CAP];

        render(&snapshot, &opts, out); /* used to fail with "capacity" from about 2000 columns up */

        CHECK_CONTAINS(out, "62% left");
        CHECK_CONTAINS(out, "as of 11:57 (3 min ago)\n");
        CHECK(strlen(out) < 600);
    }
}

static void test_reset_time_is_shown_in_the_two_most_useful_units(void)
{
    static const struct {
        long seconds;
        const char *text;
    } cases[] = {
        {2 * 3600 + 14 * 60, "resets in 2h 14m"}, {3 * 86400 + 4 * 3600, "resets in 3d 4h"},
        {45 * 60, "resets in 45m"},               {30, "resets in <1m"},
        {3600, "resets in 1h 0m"},                {86400, "resets in 1d 0h"},
        {59 * 60 + 59, "resets in 59m"},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        usage_snapshot_t snapshot = single(0.50, NOW + cases[i].seconds, false);
        chart_options_t opts = options(64, false, true);
        char out[OUT_CAP];

        render(&snapshot, &opts, out);

        CHECK_CONTAINS(out, cases[i].text);
    }
}

static void test_unknown_reset_time_shows_no_reset_text_and_no_trailing_spaces(void)
{
    usage_snapshot_t snapshot = single(0.50, 0, false);
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];

    render(&snapshot, &opts, out);

    CHECK(strstr(out, "resets") == NULL);
    CHECK_CONTAINS(out, "50% left\n");
}

static void test_age_of_the_data_is_shown_in_readable_units(void)
{
    static const struct {
        long age;
        const char *text;
    } cases[] = {
        {0, "(just now)"},        {59, "(just now)"},       {60, "(1 min ago)"},
        {3 * 60, "(3 min ago)"},  {59 * 60, "(59 min ago)"}, {3600 + 5 * 60, "(1h 05m ago)"},
        {23 * 3600, "(23h 00m ago)"}, {2 * 86400 + 5, "(2d ago)"},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        usage_snapshot_t snapshot = base_snapshot();
        chart_options_t opts = options(64, false, true);
        char out[OUT_CAP];

        snapshot.as_of = NOW - cases[i].age;
        render(&snapshot, &opts, out);

        CHECK_CONTAINS(out, cases[i].text);
    }
}

static void test_warns_when_the_data_is_more_than_half_an_hour_old(void)
{
    usage_snapshot_t fresh = base_snapshot();
    usage_snapshot_t edge = base_snapshot();
    usage_snapshot_t stale = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];

    edge.as_of = NOW - 1800;
    stale.as_of = NOW - 1801;

    render(&fresh, &opts, out);
    CHECK(strstr(out, "out of date") == NULL);
    render(&edge, &opts, out);
    CHECK(strstr(out, "out of date") == NULL);
    render(&stale, &opts, out);
    CHECK_CONTAINS(out, "Data may be out of date.\n");
}

static void test_expired_window_says_it_was_reset_instead_of_drawing_a_bar(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];

    snapshot.windows[0].expired = true;
    snapshot.windows[0].resets_at = NOW - 60;

    render(&snapshot, &opts, out);

    CHECK_CONTAINS(out, "5-hour session reset - waiting for new data\n");
    CHECK_CONTAINS(out, "Weekly");
    CHECK_CONTAINS(out, "21% left");
    CHECK(strstr(out, "Limits have reset") == NULL);
}

static void test_all_windows_expired_adds_a_summary_line(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];

    snapshot.windows[0].expired = true;
    snapshot.windows[1].expired = true;

    render(&snapshot, &opts, out);

    CHECK_CONTAINS(out, "Limits have reset. Waiting for new activity.\n");
    CHECK(strstr(out, FULL) == NULL);
}

static void test_no_windows_says_there_is_no_data_yet(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];

    snapshot.window_count = 0;

    render(&snapshot, &opts, out);

    CHECK_CONTAINS(out, "Claude - remaining usage");
    CHECK_CONTAINS(out, "No usage data yet.\n");
}

static void test_out_of_range_fractions_are_clamped_and_nan_counts_as_used_up(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char out[OUT_CAP];

    snapshot.windows[0].used_fraction = 1.7;
    snapshot.windows[1].used_fraction = NAN;
    render(&snapshot, &opts, out);
    CHECK_INT_EQ(2, count_occurrences(out, " 0% left"));

    snapshot.windows[0].used_fraction = -0.4;
    snapshot.windows[1].used_fraction = -INFINITY;
    render(&snapshot, &opts, out);
    CHECK_CONTAINS(out, "100% left");
}

static void test_extreme_timestamps_do_not_overflow(void)
{
    static const time_t extremes[] = {0, 1, -1, (time_t)INT64_MAX, (time_t)INT64_MIN};

    /* Found by the fuzzer: INT64_MAX - (-1) is undefined behaviour (the tests run under UBSan). */
    for (size_t a = 0; a < sizeof extremes / sizeof extremes[0]; a++) {
        for (size_t b = 0; b < sizeof extremes / sizeof extremes[0]; b++) {
            usage_snapshot_t snapshot = base_snapshot();
            chart_options_t opts = options(64, false, true);
            char out[OUT_CAP];

            snapshot.as_of = extremes[a];
            snapshot.windows[0].resets_at = extremes[b];
            snapshot.windows[1].resets_at = extremes[a];
            opts.now = extremes[b];

            CHECK_INT_EQ(LFCC_OK, chart_render(&snapshot, &opts, out, sizeof out));
            CHECK(out[0] != '\0');
        }
    }
}

static void test_reports_a_buffer_that_is_too_small(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    chart_options_t opts = options(64, false, true);
    char tiny[40] = "junk";
    char out[OUT_CAP];
    char exact[OUT_CAP];
    size_t needed = 0;

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, chart_render(&snapshot, &opts, tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);

    render(&snapshot, &opts, out);
    needed = strlen(out) + 1; /* text plus terminator: the exact boundary */

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, chart_render(&snapshot, &opts, tiny, 1));
    CHECK_INT_EQ(LFCC_ERR_CAPACITY, chart_render(&snapshot, &opts, exact, needed - 1));
    CHECK_INT_EQ(LFCC_OK, chart_render(&snapshot, &opts, exact, needed));
    CHECK_STR_EQ(out, exact);
}

static void test_rejects_invalid_arguments(void)
{
    usage_snapshot_t snapshot = base_snapshot();
    usage_snapshot_t too_many = base_snapshot();
    chart_options_t opts = options(64, false, true);
    chart_options_t no_title = options(64, false, true);
    char out[OUT_CAP];

    no_title.title = NULL;
    too_many.window_count = USAGE_MAX_WINDOWS + 1;

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, chart_render(NULL, &opts, out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, chart_render(&snapshot, NULL, out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, chart_render(&snapshot, &no_title, out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, chart_render(&snapshot, &opts, NULL, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, chart_render(&snapshot, &opts, out, 0));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, chart_render(&too_many, &opts, out, sizeof out));
}

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();

    RUN_TEST(test_renders_header_and_one_row_per_window);
    RUN_TEST(test_ascii_fallback_uses_plain_characters);
    RUN_TEST(test_bar_is_coloured_by_how_much_is_left);
    RUN_TEST(test_no_escape_codes_without_colour);
    RUN_TEST(test_bar_is_never_falsely_empty_or_full);
    RUN_TEST(test_bar_width_follows_the_terminal_within_limits);
    RUN_TEST(test_a_very_wide_terminal_still_draws_the_chart);
    RUN_TEST(test_reset_time_is_shown_in_the_two_most_useful_units);
    RUN_TEST(test_unknown_reset_time_shows_no_reset_text_and_no_trailing_spaces);
    RUN_TEST(test_age_of_the_data_is_shown_in_readable_units);
    RUN_TEST(test_warns_when_the_data_is_more_than_half_an_hour_old);
    RUN_TEST(test_expired_window_says_it_was_reset_instead_of_drawing_a_bar);
    RUN_TEST(test_all_windows_expired_adds_a_summary_line);
    RUN_TEST(test_no_windows_says_there_is_no_data_yet);
    RUN_TEST(test_out_of_range_fractions_are_clamped_and_nan_counts_as_used_up);
    RUN_TEST(test_extreme_timestamps_do_not_overflow);
    RUN_TEST(test_reports_a_buffer_that_is_too_small);
    RUN_TEST(test_rejects_invalid_arguments);
    return TESTKIT_RESULT();
}
