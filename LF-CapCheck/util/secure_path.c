#include "util/secure_path.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define PRIVATE_DIR_MODE 0700
#define DATA_DIR_SUFFIX "/Library/Application Support/lf-capcheck"

/* Creates the directory if needed. *created tells whether we made it. */
static lfcc_status_t make_directory_if_missing(const char *path, int *created)
{
    *created = 0;
    if (mkdir(path, PRIVATE_DIR_MODE) == 0) {
        *created = 1;
        return LFCC_OK;
    }
    switch (errno) {
    case EEXIST:
        return LFCC_OK;
    case ENOENT:
        return LFCC_ERR_NOT_FOUND;
    case ENAMETOOLONG:
        return LFCC_ERR_INVALID_ARG;
    default:
        return LFCC_ERR_IO;
    }
}

static lfcc_status_t open_directory_without_following_links(const char *path, int *fd)
{
    *fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (*fd >= 0) {
        return LFCC_OK;
    }
    switch (errno) {
    case ELOOP:     /* final component is a symlink */
    case ENOTDIR:   /* final component is not a directory */
        return LFCC_ERR_UNSAFE_PATH;
    case ENOENT:
        return LFCC_ERR_NOT_FOUND;
    case ENAMETOOLONG:
        return LFCC_ERR_INVALID_ARG;
    default:
        return LFCC_ERR_IO;
    }
}

/* Checks the already-open descriptor, so nothing can change after the check. */
static lfcc_status_t verify_private_directory(int fd)
{
    struct stat info;

    if (fstat(fd, &info) != 0) {
        return LFCC_ERR_IO;
    }
    if (!S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        return LFCC_ERR_UNSAFE_PATH;
    }
    return LFCC_OK;
}

lfcc_status_t secure_dir_open(const char *path, int *fd_out)
{
    int created = 0;
    int fd = -1;
    lfcc_status_t status = LFCC_OK;

    if (fd_out == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    *fd_out = -1;
    if (path == NULL || path[0] == '\0') {
        return LFCC_ERR_INVALID_ARG;
    }

    status = make_directory_if_missing(path, &created);
    if (status != LFCC_OK) {
        return status;
    }
    status = open_directory_without_following_links(path, &fd);
    if (status != LFCC_OK) {
        return status;
    }
    /* umask may have stripped owner bits from the mkdir mode. */
    if (created && fchmod(fd, PRIVATE_DIR_MODE) != 0) {
        close(fd);
        return LFCC_ERR_IO;
    }
    status = verify_private_directory(fd);
    if (status != LFCC_OK) {
        close(fd);
        return status;
    }

    *fd_out = fd;
    return LFCC_OK;
}

lfcc_status_t secure_default_data_dir(char *out, size_t cap)
{
    const struct passwd *entry = NULL;
    int written = 0;

    if (out == NULL || cap == 0) {
        return LFCC_ERR_INVALID_ARG;
    }
    out[0] = '\0';

    entry = getpwuid(geteuid());
    if (entry == NULL || entry->pw_dir == NULL || entry->pw_dir[0] != '/') {
        return LFCC_ERR_IO;
    }
    written = snprintf(out, cap, "%s" DATA_DIR_SUFFIX, entry->pw_dir);
    if (written < 0 || (size_t)written >= cap) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    return LFCC_OK;
}
