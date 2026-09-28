#include "testkit.h"

#include "providers/claude.h"
#include "providers/codex.h"
#include "ui/usage_view.h"

#define NOW 1738411200
#define OUT_CAP 2048
#define PLAIN_STYLE ((terminal_style_t){80, false, true})

static usage_snapshot_t snapshot_aged(long age_seconds)
{
    usage_snapshot_t usage = {0};

    usage.provider_id = "claude";
    usage.as_of = NOW - age_seconds;
    usage.window_count = 1;
    usage.windows[0] = (usage_window_t){"5-hour session", 0.38, NOW + 8040, false};
    return usage;
}

static void test_claude_has_a_refresh_hint_and_other_providers_do_not(void)
{
    CHECK(usage_view_stale_hint("claude") != NULL);
    CHECK_CONTAINS(usage_view_stale_hint("claude"), "Claude Code");
    CHECK(usage_view_stale_hint("codex") == NULL);
    CHECK(usage_view_stale_hint("something-else") == NULL);
    CHECK(usage_view_stale_hint(NULL) == NULL);
}

static void test_renders_the_chart_titled_with_the_provider_name(void)
{
    usage_snapshot_t usage = snapshot_aged(180);
    terminal_style_t style = PLAIN_STYLE;
    char out[OUT_CAP];

    CHECK_INT_EQ(LFCC_OK, usage_view_render(claude_provider(), &usage, &style, NOW, out, sizeof out));

    CHECK_CONTAINS(out, "Claude - remaining usage");
    CHECK_CONTAINS(out, "62% left");
    CHECK(strstr(out, "out of date") == NULL);
}

static void test_stale_data_gets_the_providers_hint(void)
{
    usage_snapshot_t usage = snapshot_aged(USAGE_STALE_AFTER_SECONDS + 1);
    terminal_style_t style = PLAIN_STYLE;
    char out[OUT_CAP];
    char expected[256];

    CHECK_INT_EQ(LFCC_OK, usage_view_render(claude_provider(), &usage, &style, NOW, out, sizeof out));

    snprintf(expected, sizeof expected, "Data may be out of date. %s\n", usage_view_stale_hint("claude"));
    CHECK_CONTAINS(out, expected);
}

static void test_a_provider_without_a_hint_gets_the_plain_warning(void)
{
    usage_snapshot_t usage = snapshot_aged(USAGE_STALE_AFTER_SECONDS + 1);
    terminal_style_t style = PLAIN_STYLE;
    char out[OUT_CAP];

    usage.provider_id = "codex";
    CHECK_INT_EQ(LFCC_OK, usage_view_render(codex_provider(), &usage, &style, NOW, out, sizeof out));

    CHECK_CONTAINS(out, "Data may be out of date.\n");
}

static void test_the_terminal_style_is_honoured(void)
{
    usage_snapshot_t usage = snapshot_aged(60);
    terminal_style_t fancy = {80, true, true};
    terminal_style_t ascii = {80, false, false};
    char fancy_out[OUT_CAP];
    char ascii_out[OUT_CAP];

    CHECK_INT_EQ(LFCC_OK, usage_view_render(claude_provider(), &usage, &fancy, NOW, fancy_out, sizeof fancy_out));
    CHECK_INT_EQ(LFCC_OK, usage_view_render(claude_provider(), &usage, &ascii, NOW, ascii_out, sizeof ascii_out));

    CHECK(strchr(fancy_out, '\x1b') != NULL);
    CHECK_CONTAINS(fancy_out, "\xe2\x96\x88");
    CHECK(strchr(ascii_out, '\x1b') == NULL);
    CHECK_CONTAINS(ascii_out, "#");
}

static void test_rejects_missing_arguments_and_reports_a_small_buffer(void)
{
    usage_snapshot_t usage = snapshot_aged(60);
    terminal_style_t style = PLAIN_STYLE;
    char out[OUT_CAP];
    char tiny[16] = "junk";

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, usage_view_render(NULL, &usage, &style, NOW, out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, usage_view_render(claude_provider(), NULL, &style, NOW, out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, usage_view_render(claude_provider(), &usage, NULL, NOW, out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_CAPACITY, usage_view_render(claude_provider(), &usage, &style, NOW, tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);
}

int main(void)
{
    RUN_TEST(test_claude_has_a_refresh_hint_and_other_providers_do_not);
    RUN_TEST(test_renders_the_chart_titled_with_the_provider_name);
    RUN_TEST(test_stale_data_gets_the_providers_hint);
    RUN_TEST(test_a_provider_without_a_hint_gets_the_plain_warning);
    RUN_TEST(test_the_terminal_style_is_honoured);
    RUN_TEST(test_rejects_missing_arguments_and_reports_a_small_buffer);
    return TESTKIT_RESULT();
}
