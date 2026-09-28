#include "providers/registry.h"

#include <string.h>

static bool provider_is_complete(const provider_t *provider)
{
    return provider != NULL && provider->id != NULL && provider->id[0] != '\0' &&
           provider->display_name != NULL && provider->display_name[0] != '\0' &&
           provider->load_usage != NULL;
}

static bool conflicts_with_existing(const registry_t *registry, const provider_t *candidate)
{
    for (size_t i = 0; i < registry->count; i++) {
        const provider_t *existing = registry->items[i];

        if (strcmp(existing->id, candidate->id) == 0 ||
            existing->sort_order == candidate->sort_order) {
            return true;
        }
    }
    return false;
}

/* Inserts keeping the items sorted by sort_order. The caller checked capacity. */
static void insert_sorted(registry_t *registry, const provider_t *provider)
{
    size_t position = registry->count;

    while (position > 0 && registry->items[position - 1]->sort_order > provider->sort_order) {
        registry->items[position] = registry->items[position - 1];
        position--;
    }
    registry->items[position] = provider;
    registry->count++;
}

lfcc_status_t registry_build(const provider_t *const providers[], size_t count,
                             registry_t *out)
{
    registry_t result = {{NULL}, 0};

    if (out == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    *out = result;
    if (providers == NULL || count == 0) {
        return LFCC_ERR_INVALID_ARG;
    }
    if (count > REGISTRY_MAX_PROVIDERS) {
        return LFCC_ERR_CAPACITY;
    }

    for (size_t i = 0; i < count; i++) {
        if (!provider_is_complete(providers[i]) ||
            conflicts_with_existing(&result, providers[i])) {
            return LFCC_ERR_INVALID_ARG;
        }
        insert_sorted(&result, providers[i]);
    }

    *out = result;
    return LFCC_OK;
}

size_t registry_count(const registry_t *registry)
{
    return registry != NULL ? registry->count : 0;
}

const provider_t *registry_at(const registry_t *registry, size_t index)
{
    if (registry == NULL || index >= registry->count) {
        return NULL;
    }
    return registry->items[index];
}

const provider_t *registry_find(const registry_t *registry, const char *id)
{
    if (registry == NULL || id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->items[i]->id, id) == 0) {
            return registry->items[i];
        }
    }
    return NULL;
}
