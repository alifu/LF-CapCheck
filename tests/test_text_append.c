#include "testkit.h"

#include "util/text_append.h"

#define CAP 10

static void test_appends_formatted_text_and_tracks_the_used_length(void)
{
    char buffer[CAP] = "";
    size_t used = 0;

    CHECK(text_append(buffer, sizeof buffer, &used, "%s", "abcd"));
    CHECK(text_append(buffer, sizeof buffer, &used, "%d%c", 12, 'x'));

    CHECK_STR_EQ("abcd12x", buffer);
    CHECK_INT_EQ(7, used);
}

static void test_accepts_text_that_exactly_fills_the_buffer(void)
{
    char buffer[CAP] = "";
    size_t used = 0;

    CHECK(text_append(buffer, sizeof buffer, &used, "%s", "123456789")); /* 9 + NUL */

    CHECK_STR_EQ("123456789", buffer);
    CHECK_INT_EQ(9, used);
}

static void test_rejects_text_that_would_not_fit_and_keeps_what_was_written(void)
{
    char buffer[CAP] = "";
    size_t used = 0;

    CHECK(text_append(buffer, sizeof buffer, &used, "%s", "abcdefgh"));
    CHECK(!text_append(buffer, sizeof buffer, &used, "%s", "ij")); /* needs 11 bytes */

    CHECK_STR_EQ("abcdefgh", buffer);
    CHECK_INT_EQ(8, used);
    CHECK(text_append(buffer, sizeof buffer, &used, "%s", "i"));
    CHECK_STR_EQ("abcdefghi", buffer);
}

static void test_rejects_invalid_buffers(void)
{
    char buffer[CAP] = "";
    size_t used = 0;
    size_t used_past_end = CAP;

    CHECK(!text_append(NULL, sizeof buffer, &used, "x"));
    CHECK(!text_append(buffer, sizeof buffer, NULL, "x"));
    CHECK(!text_append(buffer, 0, &used, "x"));
    CHECK(!text_append(buffer, sizeof buffer, &used_past_end, "x"));
}

int main(void)
{
    RUN_TEST(test_appends_formatted_text_and_tracks_the_used_length);
    RUN_TEST(test_accepts_text_that_exactly_fills_the_buffer);
    RUN_TEST(test_rejects_text_that_would_not_fit_and_keeps_what_was_written);
    RUN_TEST(test_rejects_invalid_buffers);
    return TESTKIT_RESULT();
}
