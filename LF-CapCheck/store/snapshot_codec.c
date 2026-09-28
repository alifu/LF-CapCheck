#include "store/snapshot_codec.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "vendor/cJSON.h"

#define PERCENT_MAX 100.0
#define MIN_CAPTURE_TIME 1 /* as_of must be a real time; resets_at may be 0 (unknown) */

/* ---- validation shared by encode and decode ---- */

static bool percent_is_valid(double percent)
{
    return isfinite(percent) && percent >= 0.0 && percent <= PERCENT_MAX;
}

static bool epoch_is_valid(long long seconds, long long minimum)
{
    return seconds >= minimum && seconds <= SNAP_MAX_EPOCH;
}

static bool window_is_valid(const snap_window_t *window)
{
    return !window->present ||
           (percent_is_valid(window->used_percentage) &&
            epoch_is_valid((long long)window->resets_at, 0));
}

static bool record_is_valid(const snap_record_t *record)
{
    return epoch_is_valid((long long)record->as_of, MIN_CAPTURE_TIME) &&
           window_is_valid(&record->five_hour) && window_is_valid(&record->seven_day) &&
           (record->five_hour.present || record->seven_day.present);
}

/* ---- encode ---- */

/* Appends formatted text; false if it would not fit (with room for the NUL). */
static bool append(char *out, size_t cap, size_t *used, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static bool append(char *out, size_t cap, size_t *used, const char *fmt, ...)
{
    va_list args;
    int written = 0;

    va_start(args, fmt);
    written = vsnprintf(out + *used, cap - *used, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= cap - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool append_window(char *out, size_t cap, size_t *used, const char *key,
                          const snap_window_t *window)
{
    if (!window->present) {
        return true;
    }
    return append(out, cap, used, ",\"%s\":{\"used_percentage\":%.6g,\"resets_at\":%lld}", key,
                  window->used_percentage, (long long)window->resets_at);
}

lfcc_status_t snap_encode(const snap_record_t *record, char *out, size_t cap, size_t *out_len)
{
    size_t used = 0;

    if (record == NULL || out == NULL || out_len == NULL || !record_is_valid(record)) {
        return LFCC_ERR_INVALID_ARG;
    }
    if (cap == 0) {
        return LFCC_ERR_CAPACITY;
    }

    if (!append(out, cap, &used, "{\"version\":%d,\"as_of\":%lld", SNAP_VERSION,
                (long long)record->as_of) ||
        !append_window(out, cap, &used, "five_hour", &record->five_hour) ||
        !append_window(out, cap, &used, "seven_day", &record->seven_day) ||
        !append(out, cap, &used, "}\n")) {
        out[0] = '\0';
        return LFCC_ERR_CAPACITY;
    }
    *out_len = used;
    return LFCC_OK;
}

/* ---- decode ---- */

static lfcc_status_t decode_epoch(const cJSON *item, long long minimum, time_t *out)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < (double)minimum || item->valuedouble > (double)SNAP_MAX_EPOCH) {
        return LFCC_ERR_PARSE;
    }
    *out = (time_t)item->valuedouble;
    return LFCC_OK;
}

/* A missing window is fine (present = false); a present one must be well formed. */
static lfcc_status_t decode_window(const cJSON *root, const char *key, snap_window_t *out)
{
    const cJSON *object = cJSON_GetObjectItemCaseSensitive(root, key);
    const cJSON *percent = NULL;
    snap_window_t window = {0};

    *out = window;
    if (object == NULL) {
        return LFCC_OK;
    }
    if (!cJSON_IsObject(object)) {
        return LFCC_ERR_PARSE;
    }

    percent = cJSON_GetObjectItemCaseSensitive(object, "used_percentage");
    if (!cJSON_IsNumber(percent) || !percent_is_valid(percent->valuedouble)) {
        return LFCC_ERR_PARSE;
    }
    if (decode_epoch(cJSON_GetObjectItemCaseSensitive(object, "resets_at"), 0,
                     &window.resets_at) != LFCC_OK) {
        return LFCC_ERR_PARSE;
    }
    window.present = true;
    window.used_percentage = percent->valuedouble;
    *out = window;
    return LFCC_OK;
}

static lfcc_status_t record_from_json(const cJSON *root, snap_record_t *out)
{
    const cJSON *version = NULL;
    snap_record_t record = {0};

    if (!cJSON_IsObject(root)) {
        return LFCC_ERR_PARSE;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valuedouble != (double)SNAP_VERSION) {
        return LFCC_ERR_PARSE;
    }
    if (decode_epoch(cJSON_GetObjectItemCaseSensitive(root, "as_of"), MIN_CAPTURE_TIME,
                     &record.as_of) != LFCC_OK ||
        decode_window(root, "five_hour", &record.five_hour) != LFCC_OK ||
        decode_window(root, "seven_day", &record.seven_day) != LFCC_OK ||
        (!record.five_hour.present && !record.seven_day.present)) {
        return LFCC_ERR_PARSE;
    }
    *out = record;
    return LFCC_OK;
}

lfcc_status_t snap_decode(const char *json, size_t length, snap_record_t *out)
{
    char text[SNAP_MAX_FILE_BYTES + 1];
    cJSON *root = NULL;
    const char *parse_end = NULL;
    lfcc_status_t status = LFCC_OK;

    if (json == NULL || out == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    if (length > SNAP_MAX_FILE_BYTES) {
        return LFCC_ERR_TOO_LARGE;
    }
    /* Bytes after an embedded NUL would be invisible to the parser. */
    if (memchr(json, '\0', length) != NULL) {
        return LFCC_ERR_PARSE;
    }

    /* cJSON needs a terminator; a private copy also rejects trailing data strictly. */
    memcpy(text, json, length);
    text[length] = '\0';
    root = cJSON_ParseWithOpts(text, &parse_end, 1);
    if (root == NULL) {
        return LFCC_ERR_PARSE;
    }

    status = record_from_json(root, out);
    cJSON_Delete(root);
    return status;
}
