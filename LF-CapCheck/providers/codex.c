#include "providers/codex.h"

static lfcc_status_t codex_load_usage(const provider_t *self, const provider_env_t *env,
                                      usage_snapshot_t *out)
{
    (void)self;
    (void)env;
    (void)out;
    return LFCC_ERR_UNAVAILABLE;
}

const provider_t *codex_provider(void)
{
    static const provider_t provider = {"codex", "Codex", PROVIDER_SORT_CODEX, codex_load_usage};

    return &provider;
}
