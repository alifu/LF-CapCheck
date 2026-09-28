#include "util/log.h"

#include <stdarg.h>

#include "util/version.h"

#define LOG_MESSAGE_MAX 512
#define ASCII_PRINTABLE_MIN 0x20
#define ASCII_DELETE 0x7f
#define UTF8_C1_LEAD 0xC2 /* U+0080..U+009F are encoded as C2 80..C2 9F */
#define UTF8_C1_FIRST 0x80
#define UTF8_C1_LAST 0x9F

static const char *level_name(log_level_t level)
{
    return level == LFCC_LOG_ERROR ? "error" : "warning";
}

static int is_c1_pair(const char *cursor)
{
    return (unsigned char)cursor[0] == UTF8_C1_LEAD && (unsigned char)cursor[1] >= UTF8_C1_FIRST &&
           (unsigned char)cursor[1] <= UTF8_C1_LAST;
}

/* Replaces control characters; every other byte (including UTF-8 text) is kept as it is. */
static void neutralize_control_characters(char *text)
{
    for (char *cursor = text; *cursor != '\0'; cursor++) {
        unsigned char byte = (unsigned char)*cursor;

        if (is_c1_pair(cursor)) {
            cursor[0] = '?';
            cursor[1] = '?';
            cursor++;
        } else if (byte < ASCII_PRINTABLE_MIN || byte == ASCII_DELETE) {
            *cursor = '?';
        }
    }
}

void log_write(FILE *stream, log_level_t level, const char *fmt, ...)
{
    char message[LOG_MESSAGE_MAX];
    va_list args;

    if (stream == NULL) {
        return;
    }
    va_start(args, fmt);
    /* vsnprintf always terminates the buffer and truncates safely. */
    vsnprintf(message, sizeof message, fmt, args);
    va_end(args);

    neutralize_control_characters(message);
    fprintf(stream, "%s: %s: %s\n", LFCC_NAME, level_name(level), message);
}
