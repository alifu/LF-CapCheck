#include "testkit.h"

#include "util/log.h"

#define OUT_CAP 2048

/* Captures what one log_write call prints. */
#define CAPTURE(out, call)                     \
    do {                                       \
        FILE *tk_stream = tmpfile();           \
        FILE *stream = tk_stream;              \
        call;                                  \
        tk_read_all(tk_stream, out, sizeof out); \
        fclose(tk_stream);                     \
    } while (0)

static void test_error_line_has_program_name_level_and_message(void)
{
    char out[OUT_CAP];

    CAPTURE(out, log_write(stream, LFCC_LOG_ERROR, "cannot open %s (%d)", "file", 7));

    CHECK_STR_EQ("lf-capcheck: error: cannot open file (7)\n", out);
}

static void test_warning_line_uses_warning_level(void)
{
    char out[OUT_CAP];

    CAPTURE(out, log_write(stream, LFCC_LOG_WARN, "slow"));

    CHECK_STR_EQ("lf-capcheck: warning: slow\n", out);
}

static void test_control_characters_are_replaced_so_output_cannot_forge_lines(void)
{
    char out[OUT_CAP];

    CAPTURE(out, log_write(stream, LFCC_LOG_ERROR, "bad \x1b[31mred\nforged: line\ttab\x7f"));

    CHECK_STR_EQ("lf-capcheck: error: bad ?[31mred?forged: line?tab?\n", out);
}

static void test_c1_control_characters_in_utf8_are_replaced_too(void)
{
    char out[OUT_CAP];

    /* U+009B (CSI) is encoded as C2 9B; some terminals treat it like ESC [. */
    CAPTURE(out, log_write(stream, LFCC_LOG_ERROR, "a\xc2\x9b[31mb\xc2\x80" "c\xc2\x9f" "d"));

    CHECK_STR_EQ("lf-capcheck: error: a??[31mb??c??d\n", out);
}

static void test_ordinary_utf8_passes_through_unchanged(void)
{
    char out[OUT_CAP];

    /* e-acute, a no-break space (C2 A0 is NOT a control character) and an euro sign. */
    CAPTURE(out, log_write(stream, LFCC_LOG_WARN, "path /caf\xc3\xa9 \xc2\xa0 \xe2\x82\xac"));

    CHECK_STR_EQ("lf-capcheck: warning: path /caf\xc3\xa9 \xc2\xa0 \xe2\x82\xac\n", out);
}

static void test_overlong_message_is_truncated_to_a_single_line(void)
{
    char out[OUT_CAP];
    char long_text[1500];
    size_t length = 0;

    memset(long_text, 'x', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = '\0';
    CAPTURE(out, log_write(stream, LFCC_LOG_ERROR, "%s", long_text));
    length = strlen(out);

    CHECK(length > 0 && length < 600);
    CHECK(out[length - 1] == '\n');
    CHECK(strchr(out, '\n') == &out[length - 1]);
}

static void test_a_missing_stream_is_ignored_instead_of_crashing(void)
{
    log_write(NULL, LFCC_LOG_ERROR, "nobody is listening");
    CHECK(1);
}

int main(void)
{
    RUN_TEST(test_error_line_has_program_name_level_and_message);
    RUN_TEST(test_warning_line_uses_warning_level);
    RUN_TEST(test_control_characters_are_replaced_so_output_cannot_forge_lines);
    RUN_TEST(test_c1_control_characters_in_utf8_are_replaced_too);
    RUN_TEST(test_ordinary_utf8_passes_through_unchanged);
    RUN_TEST(test_overlong_message_is_truncated_to_a_single_line);
    RUN_TEST(test_a_missing_stream_is_ignored_instead_of_crashing);
    return TESTKIT_RESULT();
}
