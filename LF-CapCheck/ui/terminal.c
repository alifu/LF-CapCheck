#include "ui/terminal.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DEFAULT_COLUMNS 80

static bool terminal_can_draw(const char *term_env)
{
    return term_env != NULL && term_env[0] != '\0' && strcmp(term_env, "dumb") != 0;
}

terminal_style_t terminal_style_from(bool is_tty, const char *no_color_env, const char *term_env,
                                     int columns)
{
    terminal_style_t style = {DEFAULT_COLUMNS, false, false};
    bool capable = is_tty && terminal_can_draw(term_env);
    bool colour_disabled = no_color_env != NULL && no_color_env[0] != '\0';

    if (is_tty && columns > 0) {
        style.width = columns;
    }
    style.unicode = capable;
    style.color = capable && !colour_disabled;
    return style;
}

terminal_style_t terminal_style_detect(FILE *out)
{
    int fd = fileno(out);
    bool is_tty = isatty(fd) != 0;
    struct winsize size = {0, 0, 0, 0};
    int columns = 0;

    if (is_tty && ioctl(fd, TIOCGWINSZ, &size) == 0) {
        columns = size.ws_col;
    }
    return terminal_style_from(is_tty, getenv("NO_COLOR"), getenv("TERM"), columns);
}
