#include "util/status.h"

const char *lfcc_status_str(lfcc_status_t status)
{
    switch (status) {
    case LFCC_OK:
        return "success";
    case LFCC_ERR_INVALID_ARG:
        return "invalid argument";
    case LFCC_ERR_IO:
        return "input/output error";
    case LFCC_ERR_NOT_FOUND:
        return "not found";
    case LFCC_ERR_NOT_CONNECTED:
        return "provider is not connected yet";
    case LFCC_ERR_PARSE:
        return "data is malformed or out of range";
    case LFCC_ERR_TOO_LARGE:
        return "data is too large";
    case LFCC_ERR_UNSAFE_PATH:
        return "path is not a private directory owned by you "
               "(symlink, wrong owner or loose permissions)";
    case LFCC_ERR_CAPACITY:
        return "buffer or table is too small";
    case LFCC_ERR_UNAVAILABLE:
        return "not available yet";
    }
    return "unknown error";
}
