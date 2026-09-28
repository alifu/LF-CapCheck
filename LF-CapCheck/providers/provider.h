#ifndef LFCC_PROVIDER_H
#define LFCC_PROVIDER_H

#include <time.h>

#include "providers/usage.h"

/* Menu order. Lower comes first; Claude is always first. */
#define PROVIDER_SORT_CLAUDE 0
#define PROVIDER_SORT_CODEX 10

/*
 * An AI service whose remaining usage can be shown. Instances are static
 * const tables; the menu only depends on this interface.
 */
typedef struct provider {
    const char *id;           /* stable machine name, e.g. "claude" */
    const char *display_name; /* shown in the menu, e.g. "Claude" */
    int sort_order;           /* see PROVIDER_SORT_* */

    /*
     * Fills *out from locally available data. `now` is injected so expiry
     * logic is testable. Returns LFCC_ERR_NOT_CONNECTED when the provider
     * has not been set up or has produced no data yet.
     */
    lfcc_status_t (*load_usage)(const struct provider *self, time_t now,
                                usage_snapshot_t *out);
} provider_t;

#endif /* LFCC_PROVIDER_H */
