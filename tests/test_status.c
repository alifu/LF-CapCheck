#include "testkit.h"

#include "util/status.h"

static const lfcc_status_t ALL_STATUSES[] = {
    LFCC_OK,           LFCC_ERR_INVALID_ARG,   LFCC_ERR_IO,
    LFCC_ERR_NOT_FOUND, LFCC_ERR_NOT_CONNECTED, LFCC_ERR_PARSE,
    LFCC_ERR_TOO_LARGE, LFCC_ERR_UNSAFE_PATH,   LFCC_ERR_CAPACITY,
    LFCC_ERR_UNAVAILABLE,
};
#define STATUS_COUNT (sizeof ALL_STATUSES / sizeof ALL_STATUSES[0])

static void test_ok_is_zero_so_it_works_in_conditions(void)
{
    CHECK_INT_EQ(0, LFCC_OK);
}

static void test_every_status_has_a_distinct_non_empty_message(void)
{
    for (size_t i = 0; i < STATUS_COUNT; i++) {
        const char *message = lfcc_status_str(ALL_STATUSES[i]);

        CHECK(message != NULL && message[0] != '\0');
        for (size_t j = 0; j < i; j++) {
            CHECK(strcmp(message, lfcc_status_str(ALL_STATUSES[j])) != 0);
        }
    }
}

static void test_unknown_status_has_a_fallback_message(void)
{
    CHECK_STR_EQ("unknown error", lfcc_status_str((lfcc_status_t)9999));
}

int main(void)
{
    RUN_TEST(test_ok_is_zero_so_it_works_in_conditions);
    RUN_TEST(test_every_status_has_a_distinct_non_empty_message);
    RUN_TEST(test_unknown_status_has_a_fallback_message);
    return TESTKIT_RESULT();
}
