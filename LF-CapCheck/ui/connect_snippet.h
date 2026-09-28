#ifndef LFCC_CONNECT_SNIPPET_H
#define LFCC_CONNECT_SNIPPET_H

#include <stddef.h>

#include "util/status.h"

/*
 * Builds the Claude Code settings.json snippet the user pastes to connect:
 *
 *   {
 *     "statusLine": {
 *       "type": "command",
 *       "command": "<executable> statusline"
 *     }
 *   }
 *
 * Claude Code runs `command` through a shell, so the executable path is
 * single-quoted when it contains anything but [A-Za-z0-9_./+@%:,=-], and the
 * whole command is then escaped as a JSON string. The result is therefore
 * valid JSON whose command runs exactly this program, whatever the path holds.
 *
 * LFCC_ERR_INVALID_ARG: NULL/empty or relative path, or a path that is not
 * plain UTF-8 text (invalid UTF-8, or control characters including C1).
 * LFCC_ERR_CAPACITY: `cap` too small. `out` is "" on any failure.
 */
lfcc_status_t connect_snippet_build(const char *executable, char *out, size_t cap);

#endif /* LFCC_CONNECT_SNIPPET_H */
