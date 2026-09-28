/*
 * Deterministic mutation fuzzer for every place external data enters the tool.
 * Built with ASan + UBSan (`make fuzz`), so memory errors and undefined
 * behaviour abort it; on top of that each target asserts an invariant.
 *
 *   fuzz_inputs [iterations-per-target] [seed]
 *
 * The seed is printed so any failure can be replayed exactly.
 */

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cli/statusline.h"
#include "providers/builtin.h"
#include "providers/claude_input.h"
#include "store/snapshot_store.h"
#include "testkit.h"
#include "ui/chart.h"
#include "ui/connect_snippet.h"
#include "ui/executable_path.h"
#include "ui/menu.h"
#include "ui/time_text.h"
#include "util/log.h"
#include "util/utf8.h"
#include "util/secure_path.h"
#include "vendor/cJSON.h"

#define DEFAULT_ITERATIONS 5000
#define DEFAULT_SEED 0x5EEDC0DEUL
#define NOW 1738411200
#define BUF_CAP (CLAUDE_INPUT_MAX_BYTES + 4096)
#define PATH_CAP 512

/* ---- randomness ---- */

static uint64_t rng_state = 1;
static uint64_t current_seed = 0;
static unsigned long current_iteration = 0;
static const char *current_target = "";

static uint64_t rng_next(void)
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

static size_t rng_below(size_t limit)
{
    return limit == 0 ? 0 : (size_t)(rng_next() % limit);
}

#define REQUIRE(condition)                                                          \
    do {                                                                            \
        if (!(condition)) {                                                         \
            fprintf(stderr, "FUZZ FAILURE in %s: %s\n  at %s:%d, seed %llu, iteration %lu\n", \
                    current_target, #condition, __FILE__, __LINE__,                 \
                    (unsigned long long)current_seed, current_iteration);           \
            exit(1);                                                                \
        }                                                                           \
    } while (0)

/* ---- mutation ---- */

static const char *const TOKENS[] = {
    "{", "}", "[", "]", "\"", ":", ",", "null", "true", "false", "-1", "0", "1e999", "-1e999",
    "99999999999999999999", "1738425600", "4102444801", "\\u0000", "\\", "\\\"", "rate_limits",
    "five_hour", "seven_day", "used_percentage", "resets_at", "version", "as_of", "\xc3\xa9",
    "\xff\xfe", "\n", "\r\n", "  ", "0.5", "100.0000001", "NaN", "Infinity",
};
#define TOKEN_COUNT (sizeof TOKENS / sizeof TOKENS[0])

static size_t insert_bytes(char *buffer, size_t length, size_t cap, size_t at, const char *bytes,
                           size_t count)
{
    if (length + count > cap) {
        return length;
    }
    memmove(buffer + at + count, buffer + at, length - at);
    memcpy(buffer + at, bytes, count);
    return length + count;
}

/* Applies one random change; returns the new length. */
static size_t mutate_once(char *buffer, size_t length, size_t cap)
{
    size_t at = rng_below(length + 1);

    switch (rng_below(7)) {
    case 0: /* flip one byte */
        if (length > 0) {
            buffer[rng_below(length)] ^= (char)(1u << rng_below(8));
        }
        return length;
    case 1: /* random byte */
        if (length > 0) {
            buffer[rng_below(length)] = (char)rng_below(256);
        }
        return length;
    case 2: { /* insert a dictionary token */
        const char *token = TOKENS[rng_below(TOKEN_COUNT)];

        return insert_bytes(buffer, length, cap, at, token, strlen(token));
    }
    case 3: { /* insert random bytes */
        char noise[8];
        size_t count = 1 + rng_below(sizeof noise);

        for (size_t i = 0; i < count; i++) {
            noise[i] = (char)rng_below(256);
        }
        return insert_bytes(buffer, length, cap, at, noise, count);
    }
    case 4: /* delete a range */
        if (length > 0) {
            size_t start = rng_below(length);
            size_t count = 1 + rng_below(length - start);

            memmove(buffer + start, buffer + start + count, length - start - count);
            return length - count;
        }
        return length;
    case 5: /* truncate */
        return rng_below(length + 1);
    default: { /* duplicate a chunk */
        size_t start = rng_below(length + 1);
        size_t count = rng_below(length - start + 1);
        char chunk[256];

        count = count > sizeof chunk ? sizeof chunk : count;
        memcpy(chunk, buffer + start, count);
        return insert_bytes(buffer, length, cap, at, chunk, count);
    }
    }
}

static size_t mutate(char *buffer, size_t length, size_t cap)
{
    size_t rounds = 1 + rng_below(8);

    for (size_t i = 0; i < rounds; i++) {
        length = mutate_once(buffer, length, cap);
    }
    return length;
}

static void random_bytes(char *buffer, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        buffer[i] = (char)rng_below(256);
    }
}

/* ---- seeds ---- */

static char fixture[16384];
static size_t fixture_length = 0;

static snap_record_t random_valid_record(void)
{
    snap_record_t record = {0};

    record.as_of = 1 + (time_t)rng_below(2000000000);
    record.five_hour.present = rng_below(2) == 0;
    record.seven_day.present = !record.five_hour.present || rng_below(2) == 0;
    record.five_hour.used_percentage = (double)rng_below(100001) / 1000.0;
    record.seven_day.used_percentage = (double)rng_below(100001) / 1000.0;
    record.five_hour.resets_at = (time_t)rng_below(2000000000);
    record.seven_day.resets_at = (time_t)rng_below(2000000000);
    return record;
}

/* ---- targets ---- */

static void fuzz_snapshot_decode(char *buffer)
{
    snap_record_t seed = random_valid_record();
    size_t length = 0;
    snap_record_t decoded = {0};
    char again[SNAP_MAX_FILE_BYTES];
    size_t again_length = 0;

    REQUIRE(snap_encode(&seed, buffer, SNAP_MAX_FILE_BYTES, &length) == LFCC_OK);
    if (rng_below(8) == 0) {
        length = 1 + rng_below(SNAP_MAX_FILE_BYTES);
        random_bytes(buffer, length);
    } else {
        length = mutate(buffer, length, SNAP_MAX_FILE_BYTES);
    }

    if (snap_decode(buffer, length, &decoded) == LFCC_OK) {
        /* Whatever the parser accepts must be storable and readable again. */
        REQUIRE(snap_encode(&decoded, again, sizeof again, &again_length) == LFCC_OK);
        REQUIRE(snap_decode(again, again_length, &decoded) == LFCC_OK);
    }
}

static void fuzz_claude_input(char *buffer)
{
    size_t length = 0;
    snap_record_t record = {0};

    if (rng_below(10) == 0) {
        length = rng_below(4096);
        random_bytes(buffer, length);
    } else {
        memcpy(buffer, fixture, fixture_length);
        length = mutate(buffer, fixture_length, BUF_CAP);
    }

    if (claude_input_parse(buffer, length, NOW, &record) == LFCC_OK) {
        REQUIRE(record.as_of == NOW);
        REQUIRE(record.five_hour.present || record.seven_day.present);
        for (int i = 0; i < 2; i++) {
            const snap_window_t *w = i == 0 ? &record.five_hour : &record.seven_day;

            if (w->present) {
                REQUIRE(w->used_percentage >= 0.0 && w->used_percentage <= 100.0);
                REQUIRE(w->resets_at >= 0 && w->resets_at <= SNAP_MAX_EPOCH);
            }
        }
    }
}

/* ---- statusline end to end (one data directory for the whole run) ---- */

static char statusline_dir[PATH_CAP];

static void check_printable_line(const char *text)
{
    size_t length = strlen(text);

    REQUIRE(length < 200);
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)text[i];

        REQUIRE(byte >= 0x20 || (byte == '\n' && i == length - 1));
        REQUIRE(byte != 0x7f);
    }
}

static void fuzz_statusline(char *buffer)
{
    size_t length = 0;
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    cli_io_t io = {in, out, err, statusline_dir, NOW, NULL, {80, false, false}, 0};
    char out_text[512];
    char err_text[512];
    snap_record_t stored = {0};
    int dir_fd = -1;
    lfcc_status_t status = LFCC_OK;

    if (rng_below(10) == 0) {
        length = rng_below(2048);
        random_bytes(buffer, length);
    } else {
        memcpy(buffer, fixture, fixture_length);
        length = mutate(buffer, fixture_length, BUF_CAP);
    }
    REQUIRE(fwrite(buffer, 1, length, in) == length);
    rewind(in);

    REQUIRE(statusline_run(&io) == 0);
    tk_read_all(out, out_text, sizeof out_text);
    tk_read_all(err, err_text, sizeof err_text);
    REQUIRE(err_text[0] == '\0'); /* a healthy data directory never warns */
    check_printable_line(out_text);

    /* Whatever happened, the stored snapshot must still be valid (or absent). */
    REQUIRE(secure_dir_open(statusline_dir, &dir_fd) == LFCC_OK);
    status = snap_read(dir_fd, &stored);
    close(dir_fd);
    REQUIRE(status == LFCC_OK || status == LFCC_ERR_NOT_FOUND);

    fclose(in);
    fclose(out);
    fclose(err);
}

/* ---- reading arbitrary snapshot files ---- */

static void fuzz_snapshot_file(char *buffer)
{
    char dir[PATH_CAP];
    char file[PATH_CAP + 32];
    size_t length = rng_below(SNAP_MAX_FILE_BYTES + 200);
    int dir_fd = -1;
    int fd = 0;
    snap_record_t record = {0};
    lfcc_status_t status = LFCC_OK;

    REQUIRE(tk_make_temp_dir(dir, sizeof dir) == 0);
    random_bytes(buffer, length);
    snprintf(file, sizeof file, "%s/%s", dir, SNAP_FILE_NAME);
    fd = open(file, O_CREAT | O_WRONLY, 0600);
    REQUIRE(fd >= 0);
    REQUIRE(write(fd, buffer, length) == (ssize_t)length);
    close(fd);

    REQUIRE(secure_dir_open(dir, &dir_fd) == LFCC_OK);
    status = snap_read(dir_fd, &record);
    REQUIRE(status != LFCC_OK || (record.five_hour.present || record.seven_day.present));
    REQUIRE(length <= SNAP_MAX_FILE_BYTES || status == LFCC_ERR_TOO_LARGE);
    close(dir_fd);
    tk_remove_dir(dir);
}

/* ---- menu with hostile input ---- */

static char menu_dir[PATH_CAP];

static void fuzz_menu(char *buffer)
{
    static const char *const answers[] = {"1\n", "2\n", "r\n", "b\n", "q\n", "x\n", "\n", "0\n",
                                          "99\n", "R\r\n", " 1 \n", "-1\n"};
    size_t length = 0;
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    registry_t registry;
    menu_env_t env = {in, out, menu_dir, "/opt/homebrew/bin/lf-capcheck", NOW, {80, true, true}};

    REQUIRE(builtin_registry_build(&registry) == LFCC_OK);
    for (size_t count = 1 + rng_below(12); count > 0 && length < 2000; count--) {
        if (rng_below(5) == 0) {
            size_t noise = rng_below(200);

            random_bytes(buffer + length, noise); /* may include NUL and very long lines */
            length += noise;
        } else {
            const char *answer = answers[rng_below(sizeof answers / sizeof answers[0])];

            memcpy(buffer + length, answer, strlen(answer));
            length += strlen(answer);
        }
    }
    REQUIRE(fwrite(buffer, 1, length, in) == length);
    rewind(in);
    env.style.color = rng_below(2) == 0;
    env.style.unicode = rng_below(2) == 0;
    env.style.width = (int)rng_below(300) - 10;

    REQUIRE(menu_run(&registry, &env) == MENU_EXIT_OK); /* always ends: input is finite */

    fclose(in);
    fclose(out);
}

/* ---- connect snippet and executable path ---- */

static void random_path(char *path, size_t cap)
{
    static const char *const pieces[] = {"/", "a", "b", "x", "0", " ", "'", "\"", "\\", "$", "`",
                                         ";", "&", "|", "<", ">", "(", ")", "*", "?", "[", "]",
                                         "{", "}", "~", "!", "#", "%", "=", ":", ",", ".", "+",
                                         "-", "@", "\xc3\xa9", "\t", "\n", "\x1b", "\x7f",
                                         "/Cellar/", "/bin/", "$(", "${", "\xe2\x82\xac", "\xff",
                                         "\xc2\x9b", "\xc3", "\xed\xa0\x80"};
    size_t count = rng_below(60);
    size_t used = 0;

    if (rng_below(20) == 0) {
        count = 300 + rng_below(2000);
    }
    path[0] = '\0';
    if (rng_below(10) != 0) {
        used = (size_t)snprintf(path, cap, "/");
    }
    for (size_t i = 0; i < count; i++) {
        const char *piece = pieces[rng_below(sizeof pieces / sizeof pieces[0])];
        size_t piece_length = strlen(piece);

        if (used + piece_length + 1 >= cap) {
            break;
        }
        memcpy(path + used, piece, piece_length);
        used += piece_length;
        path[used] = '\0';
    }
}

/* Reads one shell word (plain or '...' pieces joined) from `text`; returns where it ended. */
static const char *read_shell_word(const char *text, char *word, size_t cap)
{
    size_t used = 0;
    const char *cursor = text;

    while (*cursor != '\0' && *cursor != ' ') {
        if (*cursor == '\'') {
            cursor++;
            while (*cursor != '\0' && *cursor != '\'') {
                REQUIRE(used + 1 < cap);
                word[used++] = *cursor++;
            }
            REQUIRE(*cursor == '\'');
            cursor++;
        } else if (*cursor == '\\') { /* only ever produced as \' between quoted pieces */
            REQUIRE(cursor[1] == '\'');
            REQUIRE(used + 1 < cap);
            word[used++] = '\'';
            cursor += 2;
        } else {
            REQUIRE(used + 1 < cap);
            word[used++] = *cursor++;
        }
    }
    word[used] = '\0';
    return cursor;
}

static void fuzz_snippet(char *buffer)
{
    char path[4096];
    char snippet[8192];
    char word[4096];
    char stable[PATH_CAP];
    lfcc_status_t status = LFCC_OK;

    random_path(path, sizeof path);

    status = connect_snippet_build(path, snippet, sizeof snippet);
    if (status == LFCC_OK) {
        cJSON *root = cJSON_Parse(snippet);

        REQUIRE(utf8_is_plain_text(path)); /* invalid UTF-8 and C1 controls must never get through */
        const cJSON *line = cJSON_GetObjectItemCaseSensitive(root, "statusLine");
        const cJSON *command = cJSON_GetObjectItemCaseSensitive(line, "command");
        const char *end = NULL;

        REQUIRE(root != NULL && cJSON_IsString(command));
        end = read_shell_word(command->valuestring, word, sizeof word);
        REQUIRE(strcmp(word, path) == 0);               /* the shell would see exactly the path */
        REQUIRE(strcmp(end, " statusline") == 0);       /* and nothing else is smuggled in */
        cJSON_Delete(root);
    } else {
        REQUIRE(status == LFCC_ERR_INVALID_ARG || status == LFCC_ERR_CAPACITY);
        REQUIRE(snippet[0] == '\0');
    }

    status = executable_stable_path(path, stable, sizeof stable);
    REQUIRE(status == LFCC_OK || status == LFCC_ERR_CAPACITY || status == LFCC_ERR_INVALID_ARG);
    if (status == LFCC_OK) {
        REQUIRE(strlen(stable) <= strlen(path)); /* mapping only ever shortens a path */
    }
    (void)buffer;
}

/* ---- text safety: validator and logger on arbitrary bytes ---- */

static void fuzz_text_safety(char *buffer)
{
    size_t length = rng_below(300);
    FILE *stream = tmpfile();
    char written[1024];
    size_t written_length = 0;

    random_bytes(buffer, length);
    buffer[length] = '\0';
    if (rng_below(3) == 0) { /* often start from something valid and break it */
        snprintf(buffer, BUF_CAP, "caf\xc3\xa9 \xe2\x82\xac ok");
        (void)mutate(buffer, strlen(buffer), 64);
        buffer[63] = '\0';
    }

    if (utf8_is_plain_text(buffer)) {
        for (const unsigned char *c = (const unsigned char *)buffer; *c != '\0'; c++) {
            REQUIRE(*c >= 0x20 && *c != 0x7f);
        }
    }

    log_write(stream, LFCC_LOG_WARN, "%s", buffer);
    tk_read_all(stream, written, sizeof written);
    written_length = strlen(written);
    REQUIRE(written_length > 0 && written[written_length - 1] == '\n');
    for (size_t i = 0; i + 1 < written_length; i++) { /* everything before the final newline */
        unsigned char byte = (unsigned char)written[i];

        REQUIRE(byte >= 0x20 && byte != 0x7f);
        REQUIRE(!(byte == 0xC2 && (unsigned char)written[i + 1] >= 0x80 && (unsigned char)written[i + 1] <= 0x9F));
    }
    fclose(stream);
}

/* ---- chart and time text with extreme numbers ---- */

static double random_fraction(void)
{
    static const double specials[] = {NAN, INFINITY, -INFINITY, 1e308, -1e308, 1e-320, -0.0, 0.5, 1.0};

    if (rng_below(4) == 0) {
        return specials[rng_below(sizeof specials / sizeof specials[0])];
    }
    return ((double)rng_below(400001) - 100000.0) / 100000.0; /* -1.0 .. 3.0 */
}

static time_t random_time(void)
{
    static const time_t specials[] = {0, 1, -1, (time_t)INT64_MAX, (time_t)INT64_MIN,
                                      (time_t)INT32_MAX, (time_t)INT32_MIN, NOW};

    if (rng_below(4) == 0) {
        return specials[rng_below(sizeof specials / sizeof specials[0])];
    }
    return NOW + (time_t)rng_below(20000000) - 10000000;
}

static void fuzz_chart(char *buffer)
{
    usage_snapshot_t snapshot = {0};
    chart_options_t options = {0};
    char text[4096];
    size_t cap = 1 + rng_below(sizeof text);
    lfcc_status_t status = LFCC_OK;

    snapshot.provider_id = "fuzz";
    snapshot.as_of = random_time();
    snapshot.window_count = rng_below(USAGE_MAX_WINDOWS + 1);
    for (size_t i = 0; i < snapshot.window_count; i++) {
        usage_window_t *w = &snapshot.windows[i];
        size_t label_length = rng_below(USAGE_LABEL_MAX);

        for (size_t k = 0; k < label_length; k++) {
            w->label[k] = (char)(0x20 + rng_below(95));
        }
        w->used_fraction = random_fraction();
        w->resets_at = random_time();
        w->expired = rng_below(4) == 0;
    }
    options.width = rng_below(8) == 0 ? (rng_below(2) ? INT_MAX : INT_MIN) : (int)rng_below(400) - 20;
    options.color = rng_below(2) == 0;
    options.unicode = rng_below(2) == 0;
    options.now = random_time();
    options.title = "Fuzz";

    status = chart_render(&snapshot, &options, text, cap);
    REQUIRE(status == LFCC_OK || status == LFCC_ERR_CAPACITY);
    if (status == LFCC_OK) {
        size_t length = strlen(text);

        REQUIRE(length + 1 <= cap && length > 0 && text[length - 1] == '\n');
    } else {
        REQUIRE(text[0] == '\0');
    }
    (void)buffer;
}

static void fuzz_time_text(char *buffer)
{
    static const long specials[] = {0, -1, LONG_MAX, LONG_MIN, 59, 60, 3599, 3600, 86399, 86400};
    long seconds = rng_below(3) == 0 ? specials[rng_below(sizeof specials / sizeof specials[0])]
                                     : (long)rng_next();
    char text[64];

    REQUIRE(time_text_age(seconds, text, sizeof text) == LFCC_OK);
    REQUIRE(text[0] != '\0');
    (void)buffer;
}

/* ---- driver ---- */

typedef struct {
    const char *name;
    void (*run)(char *scratch);
} target_t;

int main(int argc, char *argv[])
{
    static const target_t TARGETS[] = {
        {"snapshot decode", fuzz_snapshot_decode}, {"claude input", fuzz_claude_input},
        {"statusline command", fuzz_statusline},   {"snapshot file", fuzz_snapshot_file},
        {"menu input", fuzz_menu},                 {"connect snippet + paths", fuzz_snippet},
        {"chart", fuzz_chart},                     {"time text", fuzz_time_text},
        {"text safety (utf8 + log)", fuzz_text_safety},
    };
    unsigned long iterations = argc > 1 ? strtoul(argv[1], NULL, 10) : DEFAULT_ITERATIONS;
    uint64_t seed = argc > 2 ? strtoull(argv[2], NULL, 0) : DEFAULT_SEED;
    char *scratch = malloc(BUF_CAP);
    snap_record_t record = {0};
    int fd = -1;

    REQUIRE(scratch != NULL);
    current_seed = seed;
    fixture_length = (size_t)tk_read_file("tests/fixtures/statusline_full.json", fixture, sizeof fixture);
    REQUIRE(fixture_length > 0);

    REQUIRE(tk_make_temp_dir(statusline_dir, sizeof statusline_dir) == 0);
    REQUIRE(tk_make_temp_dir(menu_dir, sizeof menu_dir) == 0);
    record.as_of = NOW - 180;
    record.five_hour = (snap_window_t){true, 38.0, NOW + 8040};
    record.seven_day = (snap_window_t){true, 79.0, NOW + 300000};
    REQUIRE(secure_dir_open(menu_dir, &fd) == LFCC_OK);
    REQUIRE(snap_write(fd, &record) == LFCC_OK);
    close(fd);

    printf("fuzzing %lu iterations per target, seed %llu\n", iterations, (unsigned long long)seed);
    for (size_t t = 0; t < sizeof TARGETS / sizeof TARGETS[0]; t++) {
        current_target = TARGETS[t].name;
        rng_state = seed * 0x9E3779B97F4A7C15ULL + t + 1;
        for (current_iteration = 0; current_iteration < iterations; current_iteration++) {
            TARGETS[t].run(scratch);
        }
        printf("  ok  %-26s %lu inputs\n", TARGETS[t].name, iterations);
    }

    tk_remove_dir(statusline_dir);
    tk_remove_dir(menu_dir);
    free(scratch);
    printf("no crashes, no undefined behaviour, all invariants held\n");
    return 0;
}
