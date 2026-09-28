#ifndef LFCC_USAGE_VIEW_H
#define LFCC_USAGE_VIEW_H

#include <stddef.h>
#include <time.h>

#include "providers/provider.h"
#include "providers/usage.h"
#include "ui/terminal.h"
#include "util/status.h"

/*
 * Practical advice for refreshing a provider's data, appended to the "Data may
 * be out of date." warning. NULL when there is none (or `provider_id` is NULL).
 */
const char *usage_view_stale_hint(const char *provider_id);

/*
 * Renders the remaining-usage chart of `usage` exactly as every screen shows
 * it: titled with the provider's name, drawn in `style`, with the provider's
 * stale hint. Shared by the menu and the watch mode.
 * LFCC_ERR_INVALID_ARG for NULL arguments; LFCC_ERR_CAPACITY if `cap` is too
 * small (out becomes "").
 */
lfcc_status_t usage_view_render(const provider_t *provider, const usage_snapshot_t *usage,
                                const terminal_style_t *style, time_t now, char *out, size_t cap);

#endif /* LFCC_USAGE_VIEW_H */
