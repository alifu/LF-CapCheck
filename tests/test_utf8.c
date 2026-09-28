#include "testkit.h"

#include "util/utf8.h"

static void test_accepts_ordinary_text_in_any_script(void)
{
    static const char *const good[] = {
        "",
        "/usr/local/bin/lf-capcheck",
        "/Users/caf\xc3\xa9/x",            /* e-acute */
        "\xc2\xa0",                         /* U+00A0 no-break space: just above the C1 range */
        "\xe2\x82\xac",                     /* euro sign */
        "\xf0\x9f\x98\x80",                 /* emoji, 4 bytes */
        "\xed\x9f\xbf",                     /* U+D7FF, last before the surrogates */
        "\xee\x80\x80",                     /* U+E000, first after the surrogates */
        "\xf4\x8f\xbf\xbf",                 /* U+10FFFF, the last code point */
        "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2", /* Cyrillic */
        "\xe6\x97\xa5\xe6\x9c\xac",         /* Japanese */
    };

    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        CHECK(utf8_is_plain_text(good[i]));
    }
}

static void test_rejects_malformed_utf8(void)
{
    static const char *const bad[] = {
        "\x80",                 /* lone continuation byte */
        "\xbf",
        "\xc3",                 /* truncated 2-byte sequence */
        "\xc3\x28",             /* bad continuation byte */
        "\xe2\x82",             /* truncated 3-byte sequence */
        "\xe2\x28\xa1",
        "\xf0\x9f\x98",         /* truncated 4-byte sequence */
        "\xc0\x80",             /* overlong encodings */
        "\xc1\xbf",
        "\xe0\x80\x80",
        "\xe0\x9f\xbf",
        "\xf0\x80\x80\x80",
        "\xf0\x8f\xbf\xbf",
        "\xed\xa0\x80",         /* UTF-16 surrogates */
        "\xed\xbf\xbf",
        "\xf4\x90\x80\x80",     /* beyond U+10FFFF */
        "\xf5\x80\x80\x80",
        "\xf8\x88\x80\x80\x80", /* 5- and 6-byte forms are not UTF-8 */
        "\xfc\x84\x80\x80\x80\x80",
        "\xfe",
        "\xff",
        "ok then \xff",
    };

    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!utf8_is_plain_text(bad[i]));
    }
}

static void test_rejects_control_characters_in_every_form(void)
{
    static const char *const controls[] = {
        "a\x01" "b", "a\nb", "a\tb", "a\rb", "a\x1b[31m", "a\x7f",       /* C0 and DEL */
        "\xc2\x80", "\xc2\x85", "\xc2\x9b", "\xc2\x9f", "a\xc2\x9b" "b", /* C1, e.g. CSI */
    };

    for (size_t i = 0; i < sizeof controls / sizeof controls[0]; i++) {
        CHECK(!utf8_is_plain_text(controls[i]));
    }
}

static void test_a_null_pointer_is_not_text(void)
{
    CHECK(!utf8_is_plain_text(NULL));
}

int main(void)
{
    RUN_TEST(test_accepts_ordinary_text_in_any_script);
    RUN_TEST(test_rejects_malformed_utf8);
    RUN_TEST(test_rejects_control_characters_in_every_form);
    RUN_TEST(test_a_null_pointer_is_not_text);
    return TESTKIT_RESULT();
}
