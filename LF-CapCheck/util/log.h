#ifndef LFCC_LOG_H
#define LFCC_LOG_H

#include <stdio.h>

typedef enum {
    LFCC_LOG_ERROR,
    LFCC_LOG_WARN
} log_level_t;

/* Redirects log output (used by tests). NULL restores stderr. */
void log_set_stream(FILE *stream);

/*
 * Writes "lf-capcheck: <level>: <message>\n" as a single line.
 * Control characters in the message (including newlines and escape
 * sequences) are replaced with '?' so untrusted text cannot forge log lines
 * or drive the terminal. Over-long messages are truncated.
 */
void log_msg(log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* LFCC_LOG_H */
