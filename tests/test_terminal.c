#include "testkit.h"

#include "ui/terminal.h"

static void test_a_terminal_gets_colour_unicode_and_its_real_width(void)
{
    terminal_style_t style = terminal_style_from(true, NULL, "xterm-256color", 132);

    CHECK(style.color);
    CHECK(style.unicode);
    CHECK_INT_EQ(132, style.width);
}

static void test_no_color_variable_turns_colour_off_but_keeps_unicode(void)
{
    terminal_style_t style = terminal_style_from(true, "1", "xterm", 100);

    CHECK(!style.color);
    CHECK(style.unicode);
}

static void test_an_empty_no_color_variable_is_ignored(void)
{
    terminal_style_t style = terminal_style_from(true, "", "xterm", 100);

    CHECK(style.color); /* the NO_COLOR convention only counts non-empty values */
}

static void test_dumb_or_unknown_terminals_get_plain_ascii(void)
{
    terminal_style_t dumb = terminal_style_from(true, NULL, "dumb", 80);
    terminal_style_t unknown = terminal_style_from(true, NULL, NULL, 80);
    terminal_style_t empty = terminal_style_from(true, NULL, "", 80);

    CHECK(!dumb.color && !dumb.unicode);
    CHECK(!unknown.color && !unknown.unicode);
    CHECK(!empty.color && !empty.unicode);
}

static void test_redirected_output_is_plain_ascii_at_default_width(void)
{
    terminal_style_t style = terminal_style_from(false, NULL, "xterm", 200);

    CHECK(!style.color);
    CHECK(!style.unicode);
    CHECK_INT_EQ(80, style.width);
}

static void test_unknown_terminal_width_falls_back_to_80(void)
{
    CHECK_INT_EQ(80, terminal_style_from(true, NULL, "xterm", 0).width);
    CHECK_INT_EQ(80, terminal_style_from(true, NULL, "xterm", -5).width);
}

static void test_detect_treats_a_plain_file_as_redirected_output(void)
{
    FILE *file = tmpfile();
    terminal_style_t style = terminal_style_detect(file);

    CHECK(!style.color);
    CHECK(!style.unicode);
    CHECK_INT_EQ(80, style.width);
    fclose(file);
}

int main(void)
{
    RUN_TEST(test_a_terminal_gets_colour_unicode_and_its_real_width);
    RUN_TEST(test_no_color_variable_turns_colour_off_but_keeps_unicode);
    RUN_TEST(test_an_empty_no_color_variable_is_ignored);
    RUN_TEST(test_dumb_or_unknown_terminals_get_plain_ascii);
    RUN_TEST(test_redirected_output_is_plain_ascii_at_default_width);
    RUN_TEST(test_unknown_terminal_width_falls_back_to_80);
    RUN_TEST(test_detect_treats_a_plain_file_as_redirected_output);
    return TESTKIT_RESULT();
}
