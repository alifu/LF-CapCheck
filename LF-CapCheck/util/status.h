#ifndef LFCC_STATUS_H
#define LFCC_STATUS_H

/* Result of every fallible operation. LFCC_OK is 0 so it reads as "no error". */
typedef enum {
    LFCC_OK = 0,
    LFCC_ERR_INVALID_ARG,   /* caller passed NULL, empty or otherwise invalid input */
    LFCC_ERR_IO,            /* the operating system reported an I/O failure */
    LFCC_ERR_NOT_FOUND,     /* a required file or directory does not exist */
    LFCC_ERR_NOT_CONNECTED, /* provider has not been set up or has no data yet */
    LFCC_ERR_PARSE,         /* external data was malformed or out of range */
    LFCC_ERR_TOO_LARGE,     /* external data exceeded a size limit */
    LFCC_ERR_UNSAFE_PATH,   /* path is a symlink, not a directory, or not private to the user */
    LFCC_ERR_CAPACITY       /* a fixed-size buffer or table is too small */
} lfcc_status_t;

/* User-friendly, never-NULL description of a status. */
const char *lfcc_status_str(lfcc_status_t status);

#endif /* LFCC_STATUS_H */
