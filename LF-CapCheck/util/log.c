#include "util/log.h"

#include <stdarg.h>

#include "util/version.h"

#define LOG_MESSAGE_MAX 512
#define ASCII_PRINTABLE_MIN 0x20
#define ASCII_DELETE 0x7f

static FILE *log_stream = NULL;

void log_set_stream(FILE *stream)
{
    log_stream = stream;
}

static const char *level_name(log_level_t level)
{
    return level == LFCC_LOG_ERROR ? "error" : "warning";
}

/* Replaces ASCII control bytes; bytes >= 0x80 (UTF-8) are kept as they are. */
static void neutralize_control_characters(char *text)
{
    for (char *cursor = text; *cursor != '\0'; cursor++) {
        unsigned char byte = (unsigned char)*cursor;

        if (byte < ASCII_PRINTABLE_MIN || byte == ASCII_DELETE) {
            *cursor = '?';
        }
    }
}

void log_msg(log_level_t level, const char *fmt, ...)
{
    char message[LOG_MESSAGE_MAX];
    FILE *stream = log_stream != NULL ? log_stream : stderr;
    va_list args;

    va_start(args, fmt);
    /* vsnprintf always terminates the buffer and truncates safely. */
    vsnprintf(message, sizeof message, fmt, args);
    va_end(args);

    neutralize_control_characters(message);
    fprintf(stream, "%s: %s: %s\n", LFCC_NAME, level_name(level), message);
}
