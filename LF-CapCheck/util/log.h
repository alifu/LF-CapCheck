#ifndef LFCC_LOG_H
#define LFCC_LOG_H

#include <stdio.h>

typedef enum {
    LFCC_LOG_ERROR,
    LFCC_LOG_WARN
} log_level_t;

/*
 * Writes "lf-capcheck: <level>: <message>\n" to `stream` as a single line.
 * Control characters in the message are replaced with '?' so untrusted text
 * cannot forge log lines or drive the terminal: ASCII controls (including
 * newline and ESC), DEL, and the C1 controls U+0080..U+009F in their UTF-8
 * form (C2 80..C2 9F). Over-long messages are truncated. A NULL stream is
 * ignored. There is no global state: callers pass the stream they own.
 */
void log_write(FILE *stream, log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#endif /* LFCC_LOG_H */
