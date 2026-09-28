#include "providers/builtin.h"

#include "providers/claude.h"
#include "providers/codex.h"

lfcc_status_t builtin_registry_build(registry_t *out)
{
    const provider_t *const providers[] = {claude_provider(), codex_provider()};

    return registry_build(providers, sizeof providers / sizeof providers[0], out);
}
