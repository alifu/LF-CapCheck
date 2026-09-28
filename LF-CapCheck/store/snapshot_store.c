#include "store/snapshot_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define PRIVATE_FILE_MODE 0600
#define TEMP_NAME_MAX 96
#define TEMP_CREATE_ATTEMPTS 8

/* ---- write ---- */

/* Exclusively creates a private temporary file next to the snapshot. */
static lfcc_status_t create_temp_file(int dir_fd, char *name, size_t cap, int *fd_out)
{
    static unsigned int counter = 0;

    for (int attempt = 0; attempt < TEMP_CREATE_ATTEMPTS; attempt++) {
        int written = snprintf(name, cap, ".%s.%ld.%u.tmp", SNAP_FILE_NAME, (long)getpid(),
                               counter++);
        int fd = -1;

        if (written < 0 || (size_t)written >= cap) {
            return LFCC_ERR_CAPACITY;
        }
        fd = openat(dir_fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                    PRIVATE_FILE_MODE);
        if (fd >= 0) {
            *fd_out = fd;
            return LFCC_OK;
        }
        if (errno != EEXIST) {
            return LFCC_ERR_IO;
        }
    }
    return LFCC_ERR_IO;
}

static lfcc_status_t write_all(int fd, const char *data, size_t length)
{
    size_t done = 0;

    while (done < length) {
        ssize_t written = write(fd, data + done, length - done);

        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return LFCC_ERR_IO;
        }
        done += (size_t)written;
    }
    return LFCC_OK;
}

/* Fills the open temp file. The mode is forced because umask may have reduced it. */
static lfcc_status_t fill_temp_file(int fd, const char *json, size_t length)
{
    if (fchmod(fd, PRIVATE_FILE_MODE) != 0) {
        return LFCC_ERR_IO;
    }
    return write_all(fd, json, length);
}

lfcc_status_t snap_write(int dir_fd, const snap_record_t *record)
{
    char json[SNAP_MAX_FILE_BYTES];
    char temp_name[TEMP_NAME_MAX];
    size_t length = 0;
    int fd = -1;
    lfcc_status_t status = LFCC_OK;

    if (dir_fd < 0 || record == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    status = snap_encode(record, json, sizeof json, &length);
    if (status != LFCC_OK) {
        return status;
    }
    status = create_temp_file(dir_fd, temp_name, sizeof temp_name, &fd);
    if (status != LFCC_OK) {
        return status;
    }

    /*
     * No fsync: Claude Code refreshes this often, so speed matters more than
     * surviving a power cut. A torn file after a crash fails validation and is
     * simply rewritten by the next update.
     */
    status = fill_temp_file(fd, json, length);
    if (close(fd) != 0 && status == LFCC_OK) {
        status = LFCC_ERR_IO;
    }
    if (status == LFCC_OK && renameat(dir_fd, temp_name, dir_fd, SNAP_FILE_NAME) != 0) {
        status = LFCC_ERR_IO;
    }
    if (status != LFCC_OK) {
        unlinkat(dir_fd, temp_name, 0);
    }
    return status;
}

/* ---- read ---- */

static lfcc_status_t open_snapshot(int dir_fd, int *fd_out)
{
    /* O_NONBLOCK so a FIFO planted at the name cannot hang us; O_NOFOLLOW refuses symlinks. */
    int fd = openat(dir_fd, SNAP_FILE_NAME, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);

    if (fd >= 0) {
        *fd_out = fd;
        return LFCC_OK;
    }
    switch (errno) {
    case ENOENT:
        return LFCC_ERR_NOT_FOUND;
    case ELOOP:
        return LFCC_ERR_UNSAFE_PATH;
    default:
        return LFCC_ERR_IO;
    }
}

/* Judged on the open descriptor, so the file cannot change between check and read. */
static lfcc_status_t verify_private_regular_file(int fd)
{
    struct stat info;

    if (fstat(fd, &info) != 0) {
        return LFCC_ERR_IO;
    }
    if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        return LFCC_ERR_UNSAFE_PATH;
    }
    if (info.st_size > SNAP_MAX_FILE_BYTES) {
        return LFCC_ERR_TOO_LARGE;
    }
    return LFCC_OK;
}

/* Reads until EOF or `cap` bytes. *length == cap means the file had more. */
static lfcc_status_t read_bounded(int fd, char *buffer, size_t cap, size_t *length)
{
    size_t used = 0;

    while (used < cap) {
        ssize_t count = read(fd, buffer + used, cap - used);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            return LFCC_ERR_IO;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }
    *length = used;
    return LFCC_OK;
}

lfcc_status_t snap_read(int dir_fd, snap_record_t *out)
{
    char buffer[SNAP_MAX_FILE_BYTES + 1];
    size_t length = 0;
    int fd = -1;
    lfcc_status_t status = LFCC_OK;

    if (dir_fd < 0 || out == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    status = open_snapshot(dir_fd, &fd);
    if (status != LFCC_OK) {
        return status;
    }

    status = verify_private_regular_file(fd);
    if (status == LFCC_OK) {
        status = read_bounded(fd, buffer, sizeof buffer, &length);
    }
    close(fd);
    if (status != LFCC_OK) {
        return status;
    }
    /* A file that grew after fstat can still exceed the limit. */
    if (length > SNAP_MAX_FILE_BYTES) {
        return LFCC_ERR_TOO_LARGE;
    }
    return snap_decode(buffer, length, out);
}
