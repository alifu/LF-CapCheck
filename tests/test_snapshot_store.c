#include "testkit.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "store/snapshot_store.h"
#include "util/secure_path.h"

#define PATH_CAP 512
#define EPSILON 1e-9
#define AS_OF 1700000000
#define FIVE_HOUR_RESET 1738425600
#define SEVEN_DAY_RESET 1738857600
#define MODE_BITS 0777

typedef struct {
    char path[PATH_CAP];
    int fd;
} store_dir_t;

static void store_dir_open(store_dir_t *dir)
{
    CHECK(tk_make_temp_dir(dir->path, sizeof dir->path) == 0);
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(dir->path, &dir->fd));
}

static void store_dir_close(store_dir_t *dir)
{
    if (dir->fd >= 0) {
        close(dir->fd);
    }
    tk_remove_dir(dir->path);
}

static void entry_path(char *out, size_t cap, const store_dir_t *dir, const char *name)
{
    snprintf(out, cap, "%s/%s", dir->path, name);
}

static int count_entries(const store_dir_t *dir)
{
    DIR *handle = opendir(dir->path);
    struct dirent *entry = NULL;
    int count = 0;

    while (handle != NULL && (entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    if (handle != NULL) {
        closedir(handle);
    }
    return count;
}

static snap_record_t sample_record(void)
{
    snap_record_t record = {0};

    record.as_of = AS_OF;
    record.five_hour = (snap_window_t){true, 23.5, FIVE_HOUR_RESET};
    record.seven_day = (snap_window_t){true, 41.2, SEVEN_DAY_RESET};
    return record;
}

/* Creates a file with exactly `mode`, regardless of umask. */
static void write_raw_file(const store_dir_t *dir, const char *name, const void *content,
                           size_t length, mode_t mode)
{
    char path[PATH_CAP];
    int fd = 0;

    entry_path(path, sizeof path, dir, name);
    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(write(fd, content, length) == (ssize_t)length);
        CHECK(fchmod(fd, mode) == 0);
        close(fd);
    }
}

static void test_write_then_read_round_trips(void)
{
    store_dir_t dir;
    snap_record_t record = sample_record();
    snap_record_t loaded = {0};

    store_dir_open(&dir);

    CHECK_INT_EQ(LFCC_OK, snap_write(dir.fd, &record));
    CHECK_INT_EQ(LFCC_OK, snap_read(dir.fd, &loaded));

    CHECK_INT_EQ(AS_OF, loaded.as_of);
    CHECK(loaded.five_hour.present && loaded.seven_day.present);
    CHECK_DOUBLE_EQ(23.5, loaded.five_hour.used_percentage, EPSILON);
    CHECK_INT_EQ(FIVE_HOUR_RESET, loaded.five_hour.resets_at);
    CHECK_DOUBLE_EQ(41.2, loaded.seven_day.used_percentage, EPSILON);
    CHECK_INT_EQ(SEVEN_DAY_RESET, loaded.seven_day.resets_at);

    store_dir_close(&dir);
}

static void test_overwrite_replaces_the_record_and_leaves_no_temp_files(void)
{
    store_dir_t dir;
    snap_record_t first = sample_record();
    snap_record_t second = sample_record();
    snap_record_t loaded = {0};

    store_dir_open(&dir);
    second.as_of = AS_OF + 60;
    second.five_hour.used_percentage = 99.0;

    CHECK_INT_EQ(LFCC_OK, snap_write(dir.fd, &first));
    CHECK_INT_EQ(LFCC_OK, snap_write(dir.fd, &second));
    CHECK_INT_EQ(LFCC_OK, snap_read(dir.fd, &loaded));

    CHECK_INT_EQ(AS_OF + 60, loaded.as_of);
    CHECK_DOUBLE_EQ(99.0, loaded.five_hour.used_percentage, EPSILON);
    CHECK_INT_EQ(1, count_entries(&dir));

    store_dir_close(&dir);
}

static void test_written_file_is_private_even_under_a_restrictive_umask(void)
{
    store_dir_t dir;
    snap_record_t record = sample_record();
    char path[PATH_CAP];
    struct stat info;
    mode_t old_umask = 0;

    store_dir_open(&dir);
    entry_path(path, sizeof path, &dir, SNAP_FILE_NAME);

    old_umask = umask(0277);
    CHECK_INT_EQ(LFCC_OK, snap_write(dir.fd, &record));
    umask(old_umask);

    CHECK(stat(path, &info) == 0);
    CHECK_INT_EQ(0600, info.st_mode & MODE_BITS);
    CHECK(S_ISREG(info.st_mode));

    store_dir_close(&dir);
}

static void test_write_rejects_an_invalid_record_and_creates_no_file(void)
{
    store_dir_t dir;
    snap_record_t empty = sample_record();
    snap_record_t out_of_range = sample_record();
    snap_record_t loaded = {0};

    store_dir_open(&dir);
    empty.five_hour.present = false;
    empty.seven_day.present = false;
    out_of_range.five_hour.used_percentage = 250.0;

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_write(dir.fd, &empty));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_write(dir.fd, &out_of_range));
    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, snap_read(dir.fd, &loaded));
    CHECK_INT_EQ(0, count_entries(&dir));

    store_dir_close(&dir);
}

static void test_read_reports_not_found_when_no_snapshot_exists(void)
{
    store_dir_t dir;
    snap_record_t loaded = {0};

    store_dir_open(&dir);

    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, snap_read(dir.fd, &loaded));

    store_dir_close(&dir);
}

static void test_read_rejects_corrupt_and_truncated_files(void)
{
    static const char *const contents[] = {
        "",
        "garbage",
        "{\"version\":1,\"as_of\":17000",
        "{\"version\":9,\"as_of\":1700000000,\"five_hour\":{\"used_percentage\":1,\"resets_at\":2}}",
    };

    for (size_t i = 0; i < sizeof contents / sizeof contents[0]; i++) {
        store_dir_t dir;
        snap_record_t loaded = {0};

        store_dir_open(&dir);
        write_raw_file(&dir, SNAP_FILE_NAME, contents[i], strlen(contents[i]), 0600);

        CHECK_INT_EQ(LFCC_ERR_PARSE, snap_read(dir.fd, &loaded));

        store_dir_close(&dir);
    }
}

static void test_read_rejects_an_oversized_file(void)
{
    store_dir_t dir;
    snap_record_t loaded = {0};
    char *big = malloc(SNAP_MAX_FILE_BYTES + 1000);

    CHECK(big != NULL);
    memset(big, ' ', SNAP_MAX_FILE_BYTES + 1000);
    store_dir_open(&dir);
    write_raw_file(&dir, SNAP_FILE_NAME, big, SNAP_MAX_FILE_BYTES + 1000, 0600);

    CHECK_INT_EQ(LFCC_ERR_TOO_LARGE, snap_read(dir.fd, &loaded));

    store_dir_close(&dir);
    free(big);
}

static void test_read_refuses_a_symlink_at_the_snapshot_name(void)
{
    store_dir_t dir;
    snap_record_t record = sample_record();
    snap_record_t loaded = {0};
    char link_path[PATH_CAP];
    char real_path[PATH_CAP];

    store_dir_open(&dir);
    CHECK_INT_EQ(LFCC_OK, snap_write(dir.fd, &record));
    entry_path(link_path, sizeof link_path, &dir, SNAP_FILE_NAME);
    entry_path(real_path, sizeof real_path, &dir, "real.json");
    CHECK(rename(link_path, real_path) == 0);
    CHECK(symlink(real_path, link_path) == 0);

    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, snap_read(dir.fd, &loaded));

    store_dir_close(&dir);
}

static void test_read_refuses_a_file_readable_by_group_or_others(void)
{
    static const mode_t loose_modes[] = {0640, 0604, 0644};

    for (size_t i = 0; i < sizeof loose_modes / sizeof loose_modes[0]; i++) {
        store_dir_t dir;
        snap_record_t record = sample_record();
        snap_record_t loaded = {0};
        char path[PATH_CAP];

        store_dir_open(&dir);
        CHECK_INT_EQ(LFCC_OK, snap_write(dir.fd, &record));
        entry_path(path, sizeof path, &dir, SNAP_FILE_NAME);
        CHECK(chmod(path, loose_modes[i]) == 0);

        CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, snap_read(dir.fd, &loaded));

        store_dir_close(&dir);
    }
}

static void test_read_refuses_a_directory_or_fifo_without_hanging(void)
{
    store_dir_t dir;
    snap_record_t loaded = {0};
    char path[PATH_CAP];

    store_dir_open(&dir);
    entry_path(path, sizeof path, &dir, SNAP_FILE_NAME);

    CHECK(mkdir(path, 0700) == 0);
    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, snap_read(dir.fd, &loaded));
    CHECK(rmdir(path) == 0);

    /* A FIFO with no writer would block a plain open() forever. */
    CHECK(mkfifo(path, 0600) == 0);
    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, snap_read(dir.fd, &loaded));

    store_dir_close(&dir);
}

static void test_write_replaces_a_symlink_instead_of_following_it(void)
{
    static const char victim_text[] = "precious data";
    store_dir_t dir;
    snap_record_t record = sample_record();
    snap_record_t loaded = {0};
    char link_path[PATH_CAP];
    char victim_path[PATH_CAP];
    char victim_read[64] = {0};
    struct stat info;
    FILE *victim = NULL;

    store_dir_open(&dir);
    write_raw_file(&dir, "victim.txt", victim_text, sizeof victim_text - 1, 0600);
    entry_path(link_path, sizeof link_path, &dir, SNAP_FILE_NAME);
    entry_path(victim_path, sizeof victim_path, &dir, "victim.txt");
    CHECK(symlink(victim_path, link_path) == 0);

    CHECK_INT_EQ(LFCC_OK, snap_write(dir.fd, &record));

    victim = fopen(victim_path, "r");
    CHECK(victim != NULL);
    if (victim != NULL) {
        CHECK(fread(victim_read, 1, sizeof victim_read - 1, victim) > 0);
        fclose(victim);
    }
    CHECK_STR_EQ(victim_text, victim_read);
    CHECK(lstat(link_path, &info) == 0);
    CHECK(S_ISREG(info.st_mode));
    CHECK_INT_EQ(LFCC_OK, snap_read(dir.fd, &loaded));

    store_dir_close(&dir);
}

#define WRITER_COUNT 4
#define WRITES_PER_WRITER 150
#define READER_ATTEMPTS 600

static snap_record_t record_for_writer(int id)
{
    snap_record_t record = {0};

    record.as_of = AS_OF + id;
    record.five_hour = (snap_window_t){true, 10.0 + id, FIVE_HOUR_RESET + id};
    record.seven_day = (snap_window_t){true, 50.0 + id, SEVEN_DAY_RESET + id};
    return record;
}

static int writer_process(int dir_fd, int id)
{
    snap_record_t record = record_for_writer(id);

    for (int i = 0; i < WRITES_PER_WRITER; i++) {
        if (snap_write(dir_fd, &record) != LFCC_OK) {
            return 1;
        }
    }
    return 0;
}

static void check_is_one_writers_complete_record(const snap_record_t *loaded)
{
    int id = (int)(loaded->as_of - AS_OF);
    snap_record_t expected = record_for_writer(id >= 0 && id < WRITER_COUNT ? id : 0);

    CHECK(id >= 0 && id < WRITER_COUNT);
    CHECK_DOUBLE_EQ(expected.five_hour.used_percentage, loaded->five_hour.used_percentage, EPSILON);
    CHECK_INT_EQ(expected.five_hour.resets_at, loaded->five_hour.resets_at);
    CHECK_DOUBLE_EQ(expected.seven_day.used_percentage, loaded->seven_day.used_percentage, EPSILON);
    CHECK_INT_EQ(expected.seven_day.resets_at, loaded->seven_day.resets_at);
}

static void test_concurrent_writers_never_expose_a_partial_snapshot(void)
{
    store_dir_t dir;
    pid_t children[WRITER_COUNT];
    int successful_reads = 0;

    store_dir_open(&dir);
    for (int id = 0; id < WRITER_COUNT; id++) {
        children[id] = fork();
        CHECK(children[id] >= 0);
        if (children[id] == 0) {
            _exit(writer_process(dir.fd, id));
        }
    }

    for (int attempt = 0; attempt < READER_ATTEMPTS; attempt++) {
        snap_record_t loaded = {0};
        lfcc_status_t status = snap_read(dir.fd, &loaded);

        if (status == LFCC_ERR_NOT_FOUND) {
            continue; /* nothing written yet */
        }
        CHECK_INT_EQ(LFCC_OK, status);
        if (status == LFCC_OK) {
            successful_reads++;
            check_is_one_writers_complete_record(&loaded);
        }
    }

    for (int id = 0; id < WRITER_COUNT; id++) {
        int wait_status = 0;

        CHECK(waitpid(children[id], &wait_status, 0) == children[id]);
        CHECK(WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0);
    }
    CHECK(successful_reads > 0);
    CHECK_INT_EQ(1, count_entries(&dir)); /* no temp files left behind */

    store_dir_close(&dir);
}

static void test_rejects_invalid_arguments(void)
{
    store_dir_t dir;
    snap_record_t record = sample_record();
    snap_record_t loaded = {0};

    store_dir_open(&dir);

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_write(-1, &record));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_write(dir.fd, NULL));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_read(-1, &loaded));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, snap_read(dir.fd, NULL));

    store_dir_close(&dir);
}

int main(void)
{
    RUN_TEST(test_write_then_read_round_trips);
    RUN_TEST(test_overwrite_replaces_the_record_and_leaves_no_temp_files);
    RUN_TEST(test_written_file_is_private_even_under_a_restrictive_umask);
    RUN_TEST(test_write_rejects_an_invalid_record_and_creates_no_file);
    RUN_TEST(test_read_reports_not_found_when_no_snapshot_exists);
    RUN_TEST(test_read_rejects_corrupt_and_truncated_files);
    RUN_TEST(test_read_rejects_an_oversized_file);
    RUN_TEST(test_read_refuses_a_symlink_at_the_snapshot_name);
    RUN_TEST(test_read_refuses_a_file_readable_by_group_or_others);
    RUN_TEST(test_read_refuses_a_directory_or_fifo_without_hanging);
    RUN_TEST(test_write_replaces_a_symlink_instead_of_following_it);
    RUN_TEST(test_concurrent_writers_never_expose_a_partial_snapshot);
    RUN_TEST(test_rejects_invalid_arguments);
    return TESTKIT_RESULT();
}
