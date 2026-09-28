#include "ui/connect_snippet.h"

#include <stdbool.h>
#include <string.h>

#include "util/text_append.h"
#include "util/utf8.h"

#define SUBCOMMAND " statusline"
#define COMMAND_MAX 1024 /* the shell command before JSON escaping */

static bool is_shell_safe(unsigned char byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || strchr("_./+@%:,=-", (int)byte) != NULL;
}

static bool needs_shell_quoting(const char *path)
{
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        if (!is_shell_safe((unsigned char)*cursor)) {
            return true;
        }
    }
    return false;
}

/* Appends `path` as one shell word: as is, or in single quotes with ' written as '\'' . */
static bool append_shell_word(char *out, size_t cap, size_t *used, const char *path)
{
    if (!needs_shell_quoting(path)) {
        return text_append(out, cap, used, "%s", path);
    }
    if (!text_append(out, cap, used, "'")) {
        return false;
    }
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        if (!text_append(out, cap, used, *cursor == '\'' ? "'\\''" : "%c", *cursor)) {
            return false;
        }
    }
    return text_append(out, cap, used, "'");
}

/* Appends `text` escaped for use inside a JSON string. The path was checked to be plain UTF-8 text. */
static bool append_json_escaped(char *out, size_t cap, size_t *used, const char *text)
{
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        bool special = *cursor == '"' || *cursor == '\\';

        if (!text_append(out, cap, used, special ? "\\%c" : "%c", *cursor)) {
            return false;
        }
    }
    return true;
}

static bool executable_is_usable(const char *executable)
{
    return executable != NULL && executable[0] == '/' && utf8_is_plain_text(executable);
}

lfcc_status_t connect_snippet_build(const char *executable, char *out, size_t cap)
{
    char command[COMMAND_MAX];
    size_t command_used = 0;
    size_t used = 0;

    if (out == NULL || cap == 0) {
        return LFCC_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!executable_is_usable(executable)) {
        return LFCC_ERR_INVALID_ARG;
    }

    /* First the raw shell command, then that command as a JSON string. */
    command[0] = '\0';
    if (!append_shell_word(command, sizeof command, &command_used, executable) ||
        !text_append(command, sizeof command, &command_used, SUBCOMMAND) ||
        !text_append(out, cap, &used, "{\n  \"statusLine\": {\n    \"type\": \"command\",\n"
                                      "    \"command\": \"") ||
        !append_json_escaped(out, cap, &used, command) ||
        !text_append(out, cap, &used, "\"\n  }\n}\n")) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    return LFCC_OK;
}
