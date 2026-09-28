#include "testkit.h"

#include "util/log.h"

#define OUT_CAP 2048

/* Captures what log_msg writes while the callback runs. */
typedef void (*log_action_t)(void);

static void capture(log_action_t action, char *buf, size_t cap)
{
    FILE *stream = tmpfile();

    log_set_stream(stream);
    action();
    log_set_stream(NULL);
    tk_read_all(stream, buf, cap);
    fclose(stream);
}

static void log_error_formatted(void)
{
    log_msg(LFCC_LOG_ERROR, "cannot open %s (%d)", "file", 7);
}

static void log_warning(void)
{
    log_msg(LFCC_LOG_WARN, "slow");
}

static void log_with_control_characters(void)
{
    log_msg(LFCC_LOG_ERROR, "bad \x1b[31mred\nforged: line\ttab\x7f");
}

static void log_with_utf8(void)
{
    log_msg(LFCC_LOG_WARN, "path /caf\xc3\xa9");
}

static void log_very_long_message(void)
{
    char long_text[1500];

    memset(long_text, 'x', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = '\0';
    log_msg(LFCC_LOG_ERROR, "%s", long_text);
}

static void test_error_line_has_program_name_level_and_message(void)
{
    char out[OUT_CAP];

    capture(log_error_formatted, out, sizeof out);

    CHECK_STR_EQ("lf-capcheck: error: cannot open file (7)\n", out);
}

static void test_warning_line_uses_warning_level(void)
{
    char out[OUT_CAP];

    capture(log_warning, out, sizeof out);

    CHECK_STR_EQ("lf-capcheck: warning: slow\n", out);
}

static void test_control_characters_are_replaced_so_output_cannot_forge_lines(void)
{
    char out[OUT_CAP];

    capture(log_with_control_characters, out, sizeof out);

    CHECK_STR_EQ("lf-capcheck: error: bad ?[31mred?forged: line?tab?\n", out);
}

static void test_utf8_text_passes_through_unchanged(void)
{
    char out[OUT_CAP];

    capture(log_with_utf8, out, sizeof out);

    CHECK_STR_EQ("lf-capcheck: warning: path /caf\xc3\xa9\n", out);
}

static void test_overlong_message_is_truncated_to_a_single_line(void)
{
    char out[OUT_CAP];
    size_t length = 0;

    capture(log_very_long_message, out, sizeof out);
    length = strlen(out);

    CHECK(length > 0 && length < 600);
    CHECK(out[length - 1] == '\n');
    CHECK(strchr(out, '\n') == &out[length - 1]);
}

int main(void)
{
    RUN_TEST(test_error_line_has_program_name_level_and_message);
    RUN_TEST(test_warning_line_uses_warning_level);
    RUN_TEST(test_control_characters_are_replaced_so_output_cannot_forge_lines);
    RUN_TEST(test_utf8_text_passes_through_unchanged);
    RUN_TEST(test_overlong_message_is_truncated_to_a_single_line);
    return TESTKIT_RESULT();
}
