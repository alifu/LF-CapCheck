#ifndef LFCC_MENU_H
#define LFCC_MENU_H

#include <stdio.h>
#include <time.h>

#include "providers/registry.h"
#include "ui/terminal.h"

#define MENU_EXIT_OK 0
#define MENU_EXIT_ERROR 1

typedef struct {
    FILE *in;               /* answers, one per line */
    FILE *out;              /* everything the menu prints */
    const char *data_dir;   /* where providers look for saved data (never created here) */
    const char *executable; /* absolute path of this program, used in the connect snippet */
    time_t now;             /* 0: the current time */
    terminal_style_t style;
} menu_env_t;

/*
 * The interactive main page:
 *
 *   LF-CapCheck - AI usage limits
 *     1) Claude  updated 3 min ago
 *     2) Codex   coming soon
 *   Choose a provider (1-2) or q to quit:
 *
 * Choosing a provider loads its usage. If it has data, the bar chart is
 * shown ([r] reload, [b] back, [q] quit). If it is not connected yet, the
 * connect screen explains the one-time setup (for Claude: the status line
 * snippet to paste) and lets you check again. Providers that are not available
 * yet, or whose data cannot be read, say so and return to the main page.
 *
 * Input is read line by line and end of input quits, so it is scriptable.
 * Returns MENU_EXIT_OK, or MENU_EXIT_ERROR if an argument is missing.
 */
int menu_run(const registry_t *registry, const menu_env_t *env);

#endif /* LFCC_MENU_H */
