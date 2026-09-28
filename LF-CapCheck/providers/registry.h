#ifndef LFCC_REGISTRY_H
#define LFCC_REGISTRY_H

#include <stddef.h>

#include "providers/provider.h"
#include "util/status.h"

#define REGISTRY_MAX_PROVIDERS 8

/* Providers in menu order. Built once by registry_build and read-only after. */
typedef struct {
    const provider_t *items[REGISTRY_MAX_PROVIDERS];
    size_t count;
} registry_t;

/*
 * Validates the providers and stores them sorted by sort_order, whatever the
 * input order. Rejects (LFCC_ERR_INVALID_ARG) NULL/incomplete providers,
 * duplicate ids and duplicate sort orders (the order would be ambiguous), and
 * an empty list. LFCC_ERR_CAPACITY when there are more than
 * REGISTRY_MAX_PROVIDERS. *out is empty on any error.
 */
lfcc_status_t registry_build(const provider_t *const providers[], size_t count,
                             registry_t *out);

size_t registry_count(const registry_t *registry);

/* NULL when the registry is NULL or the index is out of range. */
const provider_t *registry_at(const registry_t *registry, size_t index);

/* NULL when there is no provider with that id. */
const provider_t *registry_find(const registry_t *registry, const char *id);

#endif /* LFCC_REGISTRY_H */
