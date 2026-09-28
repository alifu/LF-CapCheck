#include "ui/chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/time_text.h"
#include "util/text_append.h"

#define DEFAULT_WIDTH 80
#define BAR_MIN 10
#define BAR_MAX 30
/* Columns a row needs besides label and bar: " " + bar + " " + "100% left" + "   " + reset text. */
#define ROW_OVERHEAD 30
#define STALE_AFTER_SECONDS 1800
#define GREEN_ABOVE_PERCENT 50
#define YELLOW_FROM_PERCENT 20
#define SECONDS_PER_MINUTE 60L
#define SECONDS_PER_HOUR 3600L
#define SECONDS_PER_DAY 86400L
#define TEXT_MAX 64

#define ANSI_GREEN "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_RED "\x1b[31m"
#define ANSI_RESET "\x1b[0m"

/* ---- numbers ---- */

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

/* Whole percent left. Non-finite input counts as used up rather than crashing. */
static int remaining_percent(double used_fraction)
{
    double used = isfinite(used_fraction) ? used_fraction : 1.0;

    used = used < 0.0 ? 0.0 : (used > 1.0 ? 1.0 : used);
    return (int)((1.0 - used) * 100.0 + 0.5);
}

/* A bar is never drawn empty while something is left, nor full while something is used. */
static int filled_cells(int percent_left, int bar_width)
{
    int filled = (int)(percent_left * bar_width / 100.0 + 0.5);

    if (percent_left > 0 && filled == 0) {
        filled = 1;
    }
    if (percent_left < 100 && filled >= bar_width) {
        filled = bar_width - 1;
    }
    return filled;
}

static const char *colour_for(int percent_left)
{
    if (percent_left > GREEN_ABOVE_PERCENT) {
        return ANSI_GREEN;
    }
    return percent_left >= YELLOW_FROM_PERCENT ? ANSI_YELLOW : ANSI_RED;
}

/* ---- small texts ---- */

static void format_reset(char *text, size_t cap, time_t resets_at, time_t now)
{
    long seconds = (long)(resets_at - now);

    if (resets_at == 0) {
        text[0] = '\0';
    } else if (seconds < SECONDS_PER_MINUTE) {
        snprintf(text, cap, "resets in <1m");
    } else if (seconds < SECONDS_PER_HOUR) {
        snprintf(text, cap, "resets in %ldm", seconds / SECONDS_PER_MINUTE);
    } else if (seconds < SECONDS_PER_DAY) {
        snprintf(text, cap, "resets in %ldh %ldm", seconds / SECONDS_PER_HOUR,
                 (seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE);
    } else {
        snprintf(text, cap, "resets in %ldd %ldh", seconds / SECONDS_PER_DAY,
                 (seconds % SECONDS_PER_DAY) / SECONDS_PER_HOUR);
    }
}

static void format_as_of(char *text, size_t cap, time_t as_of, time_t now)
{
    struct tm local;
    char clock_text[16] = "?";
    char age_text[TEXT_MAX];

    if (localtime_r(&as_of, &local) != NULL) {
        strftime(clock_text, sizeof clock_text, "%H:%M", &local);
    }
    time_text_age((long)(now - as_of), age_text, sizeof age_text);
    snprintf(text, cap, "as of %s (%s)", clock_text, age_text);
}

/* ---- pieces of the chart ---- */

static bool append_header(char *out, size_t cap, size_t *used, const chart_options_t *options,
                          const usage_snapshot_t *snapshot, int width)
{
    char title[TEXT_MAX];
    char meta[TEXT_MAX];
    int gap = 0;

    snprintf(title, sizeof title, "%s - remaining usage", options->title);
    format_as_of(meta, sizeof meta, snapshot->as_of, options->now);
    gap = width - (int)strlen(title) - (int)strlen(meta);
    gap = gap < 2 ? 2 : gap;

    return text_append(out, cap, used, "%s%*s%s\n", title, gap, "", meta);
}

static bool append_bar(char *out, size_t cap, size_t *used, int filled, int bar_width,
                       const char *colour, bool unicode)
{
    const char *full_cell = unicode ? "\xe2\x96\x88" : "#";
    const char *empty_cell = unicode ? "\xe2\x96\x91" : "-";

    if (colour != NULL && !text_append(out, cap, used, "%s", colour)) {
        return false;
    }
    for (int i = 0; i < bar_width; i++) {
        if (i == filled && colour != NULL && !text_append(out, cap, used, "%s", ANSI_RESET)) {
            return false;
        }
        if (!text_append(out, cap, used, "%s", i < filled ? full_cell : empty_cell)) {
            return false;
        }
    }
    /* A completely filled coloured bar has not been reset inside the loop. */
    return filled < bar_width || colour == NULL || text_append(out, cap, used, "%s", ANSI_RESET);
}

static bool append_row(char *out, size_t cap, size_t *used, const usage_window_t *window,
                       int label_width, int bar_width, const chart_options_t *options)
{
    char reset_text[TEXT_MAX];
    int percent_left = remaining_percent(window->used_fraction);
    int filled = filled_cells(percent_left, bar_width);

    if (window->expired) {
        return text_append(out, cap, used, "%-*s reset - waiting for new data\n", label_width,
                           window->label);
    }
    format_reset(reset_text, sizeof reset_text, window->resets_at, options->now);

    if (!text_append(out, cap, used, "%-*s ", label_width, window->label) ||
        !append_bar(out, cap, used, filled, bar_width,
                    options->color ? colour_for(percent_left) : NULL, options->unicode) ||
        !text_append(out, cap, used, " %3d%% left", percent_left)) {
        return false;
    }
    if (reset_text[0] != '\0' && !text_append(out, cap, used, "   %s", reset_text)) {
        return false;
    }
    return text_append(out, cap, used, "\n");
}

static bool all_expired(const usage_snapshot_t *snapshot)
{
    for (size_t i = 0; i < snapshot->window_count; i++) {
        if (!snapshot->windows[i].expired) {
            return false;
        }
    }
    return true;
}

static int widest_label(const usage_snapshot_t *snapshot)
{
    size_t widest = 0;

    for (size_t i = 0; i < snapshot->window_count; i++) {
        size_t length = strlen(snapshot->windows[i].label);

        widest = length > widest ? length : widest;
    }
    return (int)widest;
}

static bool append_body(char *out, size_t cap, size_t *used, const usage_snapshot_t *snapshot,
                        const chart_options_t *options, int width)
{
    int label_width = widest_label(snapshot);
    int bar_width = clamp_int(width - label_width - ROW_OVERHEAD, BAR_MIN, BAR_MAX);

    if (snapshot->window_count == 0) {
        return text_append(out, cap, used, "No usage data yet.\n");
    }
    for (size_t i = 0; i < snapshot->window_count; i++) {
        if (!append_row(out, cap, used, &snapshot->windows[i], label_width, bar_width, options)) {
            return false;
        }
    }
    return !all_expired(snapshot) ||
           text_append(out, cap, used, "Limits have reset. Waiting for new activity.\n");
}

lfcc_status_t chart_render(const usage_snapshot_t *snapshot, const chart_options_t *options,
                           char *out, size_t cap)
{
    size_t used = 0;
    int width = 0;

    if (snapshot == NULL || options == NULL || options->title == NULL || out == NULL ||
        cap == 0 || snapshot->window_count > USAGE_MAX_WINDOWS) {
        return LFCC_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    width = options->width > 0 ? options->width : DEFAULT_WIDTH;

    if (!append_header(out, cap, &used, options, snapshot, width) ||
        !append_body(out, cap, &used, snapshot, options, width) ||
        (options->now - snapshot->as_of > STALE_AFTER_SECONDS &&
         !text_append(out, cap, &used, "Data may be out of date.\n"))) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    return LFCC_OK;
}
