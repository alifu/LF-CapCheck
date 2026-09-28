#include "providers/claude_input.h"

#include <math.h>

#include "providers/usage.h"
#include "vendor/cJSON.h"

/* Unknown or invalid reset times are stored as 0 (= unknown). */
static time_t reset_time_or_unknown(const cJSON *item)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)SNAP_MAX_EPOCH) {
        return 0;
    }
    return (time_t)item->valuedouble;
}

/* Returns false when the window is absent or unusable. */
static bool read_window(const cJSON *limits, const char *key, snap_window_t *out)
{
    const cJSON *window = cJSON_GetObjectItemCaseSensitive(limits, key);
    const cJSON *used = NULL;
    double percent = 0.0;

    if (!cJSON_IsObject(window)) {
        return false;
    }
    used = cJSON_GetObjectItemCaseSensitive(window, "used_percentage");
    if (!cJSON_IsNumber(used) || usage_clamp_percent(used->valuedouble, &percent) != LFCC_OK) {
        return false;
    }

    out->present = true;
    out->used_percentage = percent;
    out->resets_at = reset_time_or_unknown(cJSON_GetObjectItemCaseSensitive(window, "resets_at"));
    return true;
}

static lfcc_status_t record_from_payload(const cJSON *root, time_t now, snap_record_t *out)
{
    const cJSON *limits = NULL;
    snap_record_t record = {0};

    if (!cJSON_IsObject(root)) {
        return LFCC_ERR_PARSE;
    }
    limits = cJSON_GetObjectItemCaseSensitive(root, "rate_limits");
    if (limits == NULL || cJSON_IsNull(limits)) {
        return LFCC_ERR_NOT_FOUND;
    }
    if (!cJSON_IsObject(limits)) {
        return LFCC_ERR_PARSE;
    }

    record.as_of = now;
    read_window(limits, "five_hour", &record.five_hour);
    read_window(limits, "seven_day", &record.seven_day);
    if (!record.five_hour.present && !record.seven_day.present) {
        return LFCC_ERR_NOT_FOUND;
    }
    *out = record;
    return LFCC_OK;
}

lfcc_status_t claude_input_parse(const char *json, size_t length, time_t now, snap_record_t *out)
{
    cJSON *root = NULL;
    lfcc_status_t status = LFCC_OK;

    if (json == NULL || out == NULL || now <= 0 || now > SNAP_MAX_EPOCH) {
        return LFCC_ERR_INVALID_ARG;
    }
    if (length > CLAUDE_INPUT_MAX_BYTES) {
        return LFCC_ERR_TOO_LARGE;
    }

    /* Parses exactly `length` bytes; cJSON caps nesting depth itself. */
    root = cJSON_ParseWithLength(json, length);
    if (root == NULL) {
        return LFCC_ERR_PARSE;
    }
    status = record_from_payload(root, now, out);
    cJSON_Delete(root);
    return status;
}
