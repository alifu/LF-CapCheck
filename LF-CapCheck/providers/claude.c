#include "providers/claude.h"

#include <stdio.h>
#include <unistd.h>

#include "store/snapshot_store.h"
#include "util/secure_path.h"

#define LABEL_FIVE_HOUR "5-hour session"
#define LABEL_SEVEN_DAY "Weekly"

static usage_window_t window_from_snapshot(const char *label, const snap_window_t *stored,
                                           time_t now)
{
    usage_window_t window = {{0}, 0.0, 0, false};

    snprintf(window.label, sizeof window.label, "%s", label);
    window.used_fraction = stored->used_percentage / 100.0;
    window.resets_at = stored->resets_at;
    window.expired = stored->resets_at != 0 && stored->resets_at <= now;
    return window;
}

static usage_snapshot_t usage_from_record(const char *provider_id, const snap_record_t *record,
                                          time_t now)
{
    usage_snapshot_t usage = {0};

    usage.provider_id = provider_id;
    usage.as_of = record->as_of;
    if (record->five_hour.present) {
        usage.windows[usage.window_count++] =
            window_from_snapshot(LABEL_FIVE_HOUR, &record->five_hour, now);
    }
    if (record->seven_day.present) {
        usage.windows[usage.window_count++] =
            window_from_snapshot(LABEL_SEVEN_DAY, &record->seven_day, now);
    }
    return usage;
}

/* A missing directory or snapshot just means Claude Code has not reported yet. */
static lfcc_status_t read_saved_record(const char *data_dir, snap_record_t *record)
{
    int fd = -1;
    lfcc_status_t status = secure_dir_open_existing(data_dir, &fd);

    if (status == LFCC_OK) {
        status = snap_read(fd, record);
        close(fd);
    }
    return status == LFCC_ERR_NOT_FOUND ? LFCC_ERR_NOT_CONNECTED : status;
}

static lfcc_status_t claude_load_usage(const provider_t *self, const provider_env_t *env,
                                       usage_snapshot_t *out)
{
    snap_record_t record = {0};
    lfcc_status_t status = LFCC_OK;

    if (self == NULL || env == NULL || env->data_dir == NULL || env->now <= 0 || out == NULL) {
        return LFCC_ERR_INVALID_ARG;
    }
    status = read_saved_record(env->data_dir, &record);
    if (status != LFCC_OK) {
        return status;
    }
    *out = usage_from_record(self->id, &record, env->now);
    return LFCC_OK;
}

const provider_t *claude_provider(void)
{
    static const provider_t provider = {"claude", "Claude", PROVIDER_SORT_CLAUDE,
                                        claude_load_usage};

    return &provider;
}
