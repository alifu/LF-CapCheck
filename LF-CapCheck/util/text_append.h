#ifndef LFCC_TEXT_APPEND_H
#define LFCC_TEXT_APPEND_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Appends printf-style text to `buffer` (capacity `cap`) at offset *used, and
 * advances *used. Returns false, leaving the text written so far intact and
 * NUL-terminated, if the new text plus the terminator does not fit or an
 * argument is invalid.
 */
bool text_append(char *buffer, size_t cap, size_t *used, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#endif /* LFCC_TEXT_APPEND_H */
