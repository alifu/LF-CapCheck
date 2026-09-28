#ifndef LFCC_SECURE_PATH_H
#define LFCC_SECURE_PATH_H

#include <stddef.h>

#include "util/status.h"

/*
 * Opens (creating it with mode 0700 if missing) a directory that only the
 * current user can access, and returns a file descriptor for it in *fd_out.
 * The caller closes the descriptor and should do further file work relative
 * to it (openat) so the path cannot be swapped underneath.
 *
 * Only the last path component is created; the parent must exist.
 * An existing directory is accepted only if it is a real directory (not a
 * symlink), owned by the current user, with no group/other permissions. It is
 * never re-permissioned silently.
 *
 * Returns LFCC_ERR_INVALID_ARG (bad or overlong path), LFCC_ERR_NOT_FOUND
 * (missing parent), LFCC_ERR_UNSAFE_PATH (symlink, not a directory, wrong
 * owner or loose permissions) or LFCC_ERR_IO. *fd_out is -1 on failure.
 */
lfcc_status_t secure_dir_open(const char *path, int *fd_out);

/*
 * Writes "<home>/Library/Application Support/lf-capcheck" for the current
 * user. LFCC_ERR_CAPACITY if `cap` is too small (out becomes "").
 */
lfcc_status_t secure_default_data_dir(char *out, size_t cap);

#endif /* LFCC_SECURE_PATH_H */
