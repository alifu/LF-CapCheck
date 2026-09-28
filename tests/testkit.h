#ifndef LFCC_TESTKIT_H
#define LFCC_TESTKIT_H

/* Minimal test harness: one executable per test file, no dependencies. */

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

#define CHECK_DOUBLE_EQ(expected, actual, epsilon)                           \
    do {                                                                     \
        double tk_e = (expected);                                            \
        double tk_a = (actual);                                              \
        tk_checks++;                                                         \
        if (!(fabs(tk_e - tk_a) <= (epsilon))) {                             \
            tk_failures++;                                                   \
            fprintf(stderr, "    FAIL %s:%d: %s == %g, got %g\n", __FILE__,  \
                    __LINE__, #actual, tk_e, tk_a);                          \
        }                                                                    \
    } while (0)

#define RUN_TEST(fn)                                                       \
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

/* Creates a fresh private (0700) directory under $TMPDIR. Returns 0 on success. */
static inline int tk_make_temp_dir(char *out, size_t cap)
{
    const char *tmp = getenv("TMPDIR");
    int written = 0;

    if (tmp == NULL || tmp[0] == '\0') {
        tmp = "/tmp";
    }
    written = snprintf(out, cap, "%s/lfcc-test-XXXXXX", tmp);
    if (written < 0 || (size_t)written >= cap) {
        return -1;
    }
    return mkdtemp(out) != NULL ? 0 : -1;
}

/* Reads a whole (small) file into buf, NUL-terminated. Returns its length or -1. */
static inline long tk_read_file(const char *path, char *buf, size_t cap)
{
    FILE *file = fopen(path, "rb");
    size_t used = 0;

    if (file == NULL || cap == 0) {
        return -1;
    }
    used = fread(buf, 1, cap - 1, file);
    fclose(file);
    buf[used] = '\0';
    return (long)used;
}

/* Removes every entry directly inside `path` (files, symlinks, FIFOs, empty dirs), then `path`. */
static inline void tk_remove_dir(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry = NULL;

    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            char child[1024];

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
            if (unlink(child) != 0) {
                rmdir(child);
            }
        }
        closedir(dir);
    }
    rmdir(path);
}

#endif /* LFCC_TESTKIT_H */
