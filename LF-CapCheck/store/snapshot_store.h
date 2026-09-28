#ifndef LFCC_SNAPSHOT_STORE_H
#define LFCC_SNAPSHOT_STORE_H

#include "store/snapshot_codec.h"
#include "util/status.h"

/* The only file the store touches. Callers never pass file names. */
#define SNAP_FILE_NAME "claude-usage.json"

/*
 * Both functions work relative to `dir_fd`, a descriptor for the private
 * directory returned by secure_dir_open(), so the path cannot be swapped
 * underneath them.
 */

/*
 * Saves the record atomically: the JSON goes to a private temporary file
 * (mode 0600, created exclusively) which is then renamed over the snapshot.
 * Readers therefore see either the old or the new record, never a partial
 * one, and concurrent writers cannot corrupt each other. A symlink sitting at
 * the snapshot name is replaced, never followed.
 *
 * LFCC_ERR_INVALID_ARG for a bad descriptor/record; LFCC_ERR_IO otherwise.
 * No temporary file is left behind on failure.
 */
lfcc_status_t snap_write(int dir_fd, const snap_record_t *record);

/*
 * Loads and validates the snapshot.
 * LFCC_ERR_NOT_FOUND: no snapshot yet.
 * LFCC_ERR_UNSAFE_PATH: not a regular file (symlink, directory, FIFO...),
 *   not owned by the current user, or accessible to group/others.
 * LFCC_ERR_TOO_LARGE / LFCC_ERR_PARSE: see snap_decode().
 * *out is untouched on error.
 */
lfcc_status_t snap_read(int dir_fd, snap_record_t *out);

#endif /* LFCC_SNAPSHOT_STORE_H */
