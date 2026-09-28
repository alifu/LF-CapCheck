#include "ui/usage_view.h"

#include <string.h>

#include "ui/chart.h"

#define CLAUDE_ID "claude"
#define CLAUDE_STALE_HINT "Claude Code reports your limits while it runs; use it to refresh."

const char *usage_view_stale_hint(const char *provider_id)
{
    if (provider_id != NULL && strcmp(provider_id, CLAUDE_ID) == 0) {
        return CLAUDE_STALE_HINT;
    }
    return NULL;
}

lfcc_status_t usage_view_render(const provider_t *provider, const usage_snapshot_t *usage,
                                const terminal_style_t *style, time_t now, char *out, size_t cap)
{
    chart_options_t options = {0};

    if (provider == NULL || usage == NULL || style == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    options.width = style->width;
    options.color = style->color;
    options.unicode = style->unicode;
    options.now = now;
    options.title = provider->display_name;
    options.stale_hint = usage_view_stale_hint(provider->id);
    return chart_render(usage, &options, out, cap);
}
