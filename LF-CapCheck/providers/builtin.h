#ifndef LFCC_BUILTIN_H
#define LFCC_BUILTIN_H

#include "providers/registry.h"

/* The providers shipped with the tool, in menu order (Claude first). */
lfcc_status_t builtin_registry_build(registry_t *out);

#endif /* LFCC_BUILTIN_H */
