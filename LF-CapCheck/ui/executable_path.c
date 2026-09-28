#include "ui/executable_path.h"

#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELLAR_MARKER "/Cellar/"

/* Length of the next path segment, or 0 if it is empty or missing. */
static size_t segment_length(const char *text)
{
    const char *end = strchr(text, '/');

    return end != NULL ? (size_t)(end - text) : strlen(text);
}

/*
 * `after_cellar` is what follows "/Cellar/". Returns the binary name when it
 * looks like "<formula>/<version>/bin/<name>", otherwise NULL.
 */
static const char *binary_name_in_keg(const char *after_cellar)
{
    const char *cursor = after_cellar;

    for (int segment = 0; segment < 2; segment++) { /* formula, then version */
        size_t length = segment_length(cursor);

        if (length == 0 || cursor[length] != '/') {
            return NULL;
        }
        cursor += length + 1;
    }
    if (strncmp(cursor, "bin/", 4) != 0) {
        return NULL;
    }
    cursor += 4;
    return (cursor[0] != '\0' && strchr(cursor, '/') == NULL) ? cursor : NULL;
}

lfcc_status_t executable_stable_path(const char *path, char *out, size_t cap)
{
    const char *marker = NULL;
    const char *name = NULL;
    int written = 0;

    if (out == NULL || cap == 0) {
        return LFCC_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (path == NULL || path[0] == '\0') {
        return LFCC_ERR_INVALID_ARG;
    }

    marker = strstr(path, CELLAR_MARKER);
    name = (marker != NULL && marker != path)
               ? binary_name_in_keg(marker + strlen(CELLAR_MARKER))
               : NULL;

    if (name != NULL) {
        written = snprintf(out, cap, "%.*s/bin/%s", (int)(marker - path), path, name);
    } else {
        written = snprintf(out, cap, "%s", path);
    }
    if (written < 0 || (size_t)written >= cap) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    return LFCC_OK;
}

lfcc_status_t executable_current_path(char *out, size_t cap)
{
    char raw[PATH_MAX];
    char resolved[PATH_MAX];
    uint32_t raw_size = sizeof raw;
    int written = 0;

    if (out == NULL || cap == 0) {
        return LFCC_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (_NSGetExecutablePath(raw, &raw_size) != 0 || realpath(raw, resolved) == NULL) {
        return LFCC_ERR_IO;
    }
    written = snprintf(out, cap, "%s", resolved);
    if (written < 0 || (size_t)written >= cap) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    return LFCC_OK;
}
