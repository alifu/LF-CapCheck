#include "util/text_append.h"

#include <stdarg.h>
#include <stdio.h>

bool text_append(char *buffer, size_t cap, size_t *used, const char *fmt, ...)
{
    va_list args;
    size_t room = 0;
    int written = 0;

    if (buffer == NULL || used == NULL || cap == 0 || *used >= cap) {
        return false;
    }
    room = cap - *used;

    va_start(args, fmt);
    written = vsnprintf(buffer + *used, room, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= room) {
        buffer[*used] = '\0'; /* drop the truncated tail; keep a valid string */
        return false;
    }
    *used += (size_t)written;
    return true;
}
