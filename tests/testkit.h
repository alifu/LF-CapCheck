#ifndef LFCC_TESTKIT_H
#define LFCC_TESTKIT_H

/* Minimal test harness: one executable per test file, no dependencies. */

#include <stdio.h>
#include <string.h>

static int tk_checks = 0;
static int tk_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        tk_checks++;                                                         \
        if (!(cond)) {                                                       \
            tk_failures++;                                                   \
            fprintf(stderr, "    FAIL %s:%d: %s\n", __FILE__, __LINE__,      \
                    #cond);                                                  \
        }                                                                    \
    } while (0)

#define CHECK_INT_EQ(expected, actual)                                       \
    do {                                                                     \
        long tk_e = (long)(expected);                                        \
        long tk_a = (long)(actual);                                          \
        tk_checks++;                                                         \
        if (tk_e != tk_a) {                                                  \
            tk_failures++;                                                   \
            fprintf(stderr, "    FAIL %s:%d: %s == %ld, got %ld\n",          \
                    __FILE__, __LINE__, #actual, tk_e, tk_a);                \
        }                                                                    \
    } while (0)

#define CHECK_STR_EQ(expected, actual)                                       \
    do {                                                                     \
        const char *tk_e = (expected);                                       \
        const char *tk_a = (actual);                                         \
        tk_checks++;                                                         \
        if (strcmp(tk_e, tk_a) != 0) {                                       \
            tk_failures++;                                                   \
            fprintf(stderr, "    FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                    __FILE__, __LINE__, tk_e, tk_a);                         \
        }                                                                    \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                     \
    do {                                                                     \
        const char *tk_h = (haystack);                                       \
        const char *tk_n = (needle);                                         \
        tk_checks++;                                                         \
        if (strstr(tk_h, tk_n) == NULL) {                                    \
            tk_failures++;                                                   \
            fprintf(stderr, "    FAIL %s:%d: \"%s\" not found in \"%s\"\n",  \
                    __FILE__, __LINE__, tk_n, tk_h);                         \
        }                                                                    \
    } while (0)

#define RUN_TEST(fn)                                                         \
    do {                                                                     \
        int tk_before = tk_failures;                                         \
        fn();                                                                \
        fprintf(stderr, "  %s %s\n", tk_failures == tk_before ? "ok  " : "FAIL", #fn); \
    } while (0)

/* Returns the process exit code: 0 when every check passed. */
#define TESTKIT_RESULT()                                                     \
    (fprintf(stderr, "%s: %d checks, %d failures\n", __FILE__, tk_checks,    \
             tk_failures),                                                   \
     tk_failures == 0 ? 0 : 1)

/* Read everything written to a tmpfile() into buf (NUL-terminated). */
static inline void tk_read_all(FILE *stream, char *buf, size_t cap)
{
    size_t used = 0;
    rewind(stream);
    if (cap == 0) {
        return;
    }
    used = fread(buf, 1, cap - 1, stream);
    buf[used] = '\0';
}

#endif /* LFCC_TESTKIT_H */
