#include "testkit.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "providers/claude.h"
#include "store/snapshot_store.h"
#include "util/secure_path.h"

#define NOW 1738400000
#define FIVE_HOUR_RESET 1738425600
#define SEVEN_DAY_RESET 1738857600
#define PATH_CAP 512
#define EPSILON 1e-9

static void save_record(const char *data_dir, const snap_record_t *record)
{
    int fd = -1;

    CHECK_INT_EQ(LFCC_OK, secure_dir_open(data_dir, &fd));
    CHECK_INT_EQ(LFCC_OK, snap_write(fd, record));
    close(fd);
}

static snap_record_t sample_record(void)
{
    snap_record_t record = {0};

    record.as_of = NOW - 180;
    record.five_hour = (snap_window_t){true, 23.5, FIVE_HOUR_RESET};
    record.seven_day = (snap_window_t){true, 41.2, SEVEN_DAY_RESET};
    return record;
}

static lfcc_status_t load(const char *data_dir, time_t now, usage_snapshot_t *out)
{
    const provider_t *claude = claude_provider();
    provider_env_t env = {now, data_dir};

    return claude->load_usage(claude, &env, out);
}

static void test_identity_puts_claude_first_in_the_menu(void)
{
    const provider_t *claude = claude_provider();

    CHECK_STR_EQ("claude", claude->id);
    CHECK_STR_EQ("Claude", claude->display_name);
    CHECK_INT_EQ(PROVIDER_SORT_CLAUDE, claude->sort_order);
    CHECK(claude->load_usage != NULL);
}

static void test_maps_the_saved_record_to_a_usage_snapshot(void)
{
    char dir[PATH_CAP];
    snap_record_t record = sample_record();
    usage_snapshot_t usage = {0};

    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    save_record(dir, &record);

    CHECK_INT_EQ(LFCC_OK, load(dir, NOW, &usage));

    CHECK_STR_EQ("claude", usage.provider_id);
    CHECK_INT_EQ(NOW - 180, usage.as_of);
    CHECK_INT_EQ(2, usage.window_count);
    CHECK_STR_EQ("5-hour session", usage.windows[0].label);
    CHECK_DOUBLE_EQ(0.235, usage.windows[0].used_fraction, EPSILON);
    CHECK_INT_EQ(FIVE_HOUR_RESET, usage.windows[0].resets_at);
    CHECK(!usage.windows[0].expired);
    CHECK_STR_EQ("Weekly", usage.windows[1].label);
    CHECK_DOUBLE_EQ(0.412, usage.windows[1].used_fraction, EPSILON);
    CHECK_INT_EQ(SEVEN_DAY_RESET, usage.windows[1].resets_at);

    tk_remove_dir(dir);
}

static void test_lists_only_the_windows_that_were_saved(void)
{
    char dir[PATH_CAP];
    snap_record_t record = sample_record();
    usage_snapshot_t usage = {0};

    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    record.five_hour.present = false;
    save_record(dir, &record);

    CHECK_INT_EQ(LFCC_OK, load(dir, NOW, &usage));

    CHECK_INT_EQ(1, usage.window_count);
    CHECK_STR_EQ("Weekly", usage.windows[0].label);

    tk_remove_dir(dir);
}

static void test_marks_windows_whose_reset_time_has_passed_as_expired(void)
{
    char dir[PATH_CAP];
    snap_record_t record = sample_record();
    usage_snapshot_t usage = {0};

    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    record.seven_day.resets_at = 0; /* unknown reset time never expires */
    save_record(dir, &record);

    CHECK_INT_EQ(LFCC_OK, load(dir, FIVE_HOUR_RESET, &usage)); /* exactly at the reset */
    CHECK(usage.windows[0].expired);
    CHECK(!usage.windows[1].expired);

    CHECK_INT_EQ(LFCC_OK, load(dir, FIVE_HOUR_RESET - 1, &usage));
    CHECK(!usage.windows[0].expired);

    tk_remove_dir(dir);
}

static void test_reports_not_connected_when_nothing_has_been_saved_yet(void)
{
    char dir[PATH_CAP];
    char missing[PATH_CAP];
    usage_snapshot_t usage = {0};
    struct stat info;

    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    snprintf(missing, sizeof missing, "%s/never-created", dir);

    CHECK_INT_EQ(LFCC_ERR_NOT_CONNECTED, load(dir, NOW, &usage));      /* empty directory */
    CHECK_INT_EQ(LFCC_ERR_NOT_CONNECTED, load(missing, NOW, &usage));  /* no directory */
    CHECK(stat(missing, &info) != 0); /* reading must not create anything */

    tk_remove_dir(dir);
}

static void test_passes_through_unreadable_or_unsafe_data(void)
{
    char dir[PATH_CAP];
    char file[PATH_CAP];
    snap_record_t record = sample_record();
    usage_snapshot_t usage = {0};
    int fd = -1;

    CHECK(tk_make_temp_dir(dir, sizeof dir) == 0);
    snprintf(file, sizeof file, "%s/%s", dir, SNAP_FILE_NAME);

    fd = open(file, O_CREAT | O_WRONLY, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, "garbage", 7) == 7);
    close(fd);
    CHECK_INT_EQ(LFCC_ERR_PARSE, load(dir, NOW, &usage));
    unlink(file);

    save_record(dir, &record);
    CHECK(chmod(file, 0644) == 0);
    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, load(dir, NOW, &usage));

    CHECK(chmod(file, 0600) == 0);
    CHECK(chmod(dir, 0755) == 0);
    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, load(dir, NOW, &usage));

    tk_remove_dir(dir);
}

static void test_rejects_invalid_arguments(void)
{
    const provider_t *claude = claude_provider();
    provider_env_t good = {NOW, "/tmp"};
    provider_env_t no_dir = {NOW, NULL};
    provider_env_t no_time = {0, "/tmp"};
    usage_snapshot_t usage = {0};

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude->load_usage(claude, NULL, &usage));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude->load_usage(claude, &no_dir, &usage));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude->load_usage(claude, &no_time, &usage));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude->load_usage(claude, &good, NULL));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, claude->load_usage(NULL, &good, &usage));
}

int main(void)
{
    RUN_TEST(test_identity_puts_claude_first_in_the_menu);
    RUN_TEST(test_maps_the_saved_record_to_a_usage_snapshot);
    RUN_TEST(test_lists_only_the_windows_that_were_saved);
    RUN_TEST(test_marks_windows_whose_reset_time_has_passed_as_expired);
    RUN_TEST(test_reports_not_connected_when_nothing_has_been_saved_yet);
    RUN_TEST(test_passes_through_unreadable_or_unsafe_data);
    RUN_TEST(test_rejects_invalid_arguments);
    return TESTKIT_RESULT();
}
