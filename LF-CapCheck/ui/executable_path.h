#ifndef LFCC_EXECUTABLE_PATH_H
#define LFCC_EXECUTABLE_PATH_H

#include <stddef.h>

#include "util/status.h"

/*
 * Maps a versioned Homebrew path
 *   <prefix>/Cellar/<formula>/<version>/bin/<name>
 * to the stable <prefix>/bin/<name> that survives `brew upgrade`. Any other
 * path is returned unchanged (a missing prefix counts as "other").
 * LFCC_ERR_INVALID_ARG for NULL/empty input; LFCC_ERR_CAPACITY if `cap` is
 * too small (out becomes "").
 */
lfcc_status_t executable_stable_path(const char *path, char *out, size_t cap);

/*
 * Absolute, symlink-resolved path of the running program.
 * LFCC_ERR_CAPACITY if `cap` is too small; LFCC_ERR_IO if it cannot be found.
 */
lfcc_status_t executable_current_path(char *out, size_t cap);

#endif /* LFCC_EXECUTABLE_PATH_H */
