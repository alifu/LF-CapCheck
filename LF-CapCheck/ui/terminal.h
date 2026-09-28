#ifndef LFCC_TERMINAL_H
#define LFCC_TERMINAL_H

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    int width;    /* columns; 80 when unknown or not a terminal */
    bool color;   /* ANSI colours allowed */
    bool unicode; /* block characters allowed (otherwise plain ASCII) */
} terminal_style_t;

/*
 * Decides how to draw from plain facts, so the rules are testable:
 * - not a terminal (redirected/piped): plain ASCII, no colour, 80 columns;
 * - TERM missing, empty or "dumb": plain ASCII, no colour;
 * - a non-empty NO_COLOR turns colour off (https://no-color.org) but keeps Unicode.
 */
terminal_style_t terminal_style_from(bool is_tty, const char *no_color_env, const char *term_env,
                                     int columns);

/* Reads the real terminal size and environment for `out`. */
terminal_style_t terminal_style_detect(FILE *out);

#endif /* LFCC_TERMINAL_H */
