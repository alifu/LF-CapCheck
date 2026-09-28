#include "testkit.h"

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/secure_path.h"

#define PATH_CAP 512
#define PATH_MAX_TEST 5000 /* longer than any macOS path (PATH_MAX is 1024) */
#define MODE_BITS 0777

typedef struct {
    char base[PATH_CAP];
    char target[PATH_CAP];
} fixture_t;

static void fixture_init(fixture_t *fx, const char *name)
{
    int ok = tk_make_temp_dir(fx->base, sizeof fx->base);

    CHECK(ok == 0);
    snprintf(fx->target, sizeof fx->target, "%s/%s", fx->base, name);
}

static void fixture_cleanup(const fixture_t *fx)
{
    /* Removes whatever the test created: file, symlink or directory. */
    if (unlink(fx->target) != 0) {
        rmdir(fx->target);
    }
    rmdir(fx->base);
}

static mode_t mode_of(const char *path)
{
    struct stat info;

    return stat(path, &info) == 0 ? (mode_t)(info.st_mode & MODE_BITS) : 0;
}

static void test_creates_missing_directory_with_private_mode(void)
{
    fixture_t fx;
    int fd = -1;
    mode_t old_umask = 0;

    fixture_init(&fx, "data");
    old_umask = umask(0);
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(fx.target, &fd));
    CHECK(fd >= 0);
    CHECK_INT_EQ(0700, mode_of(fx.target));

    umask(old_umask);
    if (fd >= 0) {
        close(fd);
    }
    fixture_cleanup(&fx);
}

static void test_creates_usable_directory_even_when_umask_strips_owner_bits(void)
{
    fixture_t fx;
    int fd = -1;
    mode_t old_umask = 0;

    /* The fixture is created first: the umask under test must not affect it. */
    fixture_init(&fx, "data");
    old_umask = umask(0277);
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(fx.target, &fd));
    CHECK_INT_EQ(0700, mode_of(fx.target));

    umask(old_umask);
    if (fd >= 0) {
        close(fd);
    }
    fixture_cleanup(&fx);
}

static void test_accepts_existing_private_directory(void)
{
    fixture_t fx;
    int fd = -1;

    fixture_init(&fx, "data");
    CHECK(mkdir(fx.target, 0700) == 0);

    CHECK_INT_EQ(LFCC_OK, secure_dir_open(fx.target, &fd));
    CHECK(fd >= 0);

    if (fd >= 0) {
        close(fd);
    }
    fixture_cleanup(&fx);
}

static void test_rejects_existing_directory_open_to_group_or_others(void)
{
    static const mode_t loose_modes[] = {0755, 0770, 0707};

    for (size_t i = 0; i < sizeof loose_modes / sizeof loose_modes[0]; i++) {
        fixture_t fx;
        int fd = -1;

        fixture_init(&fx, "data");
        CHECK(mkdir(fx.target, 0700) == 0);
        CHECK(chmod(fx.target, loose_modes[i]) == 0);

        CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, secure_dir_open(fx.target, &fd));
        CHECK_INT_EQ(-1, fd);
        /* Existing directories are never silently re-permissioned. */
        CHECK_INT_EQ(loose_modes[i], mode_of(fx.target));

        fixture_cleanup(&fx);
    }
}

static void test_rejects_symlink_even_when_it_points_to_a_private_directory(void)
{
    fixture_t fx;
    char real_dir[PATH_CAP];
    int fd = -1;

    fixture_init(&fx, "link");
    snprintf(real_dir, sizeof real_dir, "%s/real", fx.base);
    CHECK(mkdir(real_dir, 0700) == 0);
    CHECK(symlink(real_dir, fx.target) == 0);

    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, secure_dir_open(fx.target, &fd));
    CHECK_INT_EQ(-1, fd);

    unlink(fx.target);
    rmdir(real_dir);
    rmdir(fx.base);
}

static void test_rejects_dangling_symlink(void)
{
    fixture_t fx;
    int fd = -1;

    fixture_init(&fx, "link");
    CHECK(symlink("/nonexistent/lfcc-target", fx.target) == 0);

    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, secure_dir_open(fx.target, &fd));
    CHECK_INT_EQ(-1, fd);

    fixture_cleanup(&fx);
}

static void test_rejects_regular_file_where_directory_is_expected(void)
{
    fixture_t fx;
    int fd = -1;
    int file_fd = 0;

    fixture_init(&fx, "file");
    file_fd = open(fx.target, O_CREAT | O_WRONLY, 0600);
    CHECK(file_fd >= 0);
    close(file_fd);

    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, secure_dir_open(fx.target, &fd));
    CHECK_INT_EQ(-1, fd);

    fixture_cleanup(&fx);
}

static void test_reports_not_found_when_parent_directory_is_missing(void)
{
    fixture_t fx;
    char nested[PATH_CAP];
    int fd = -1;

    fixture_init(&fx, "missing");
    snprintf(nested, sizeof nested, "%s/child", fx.target);

    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, secure_dir_open(nested, &fd));
    CHECK_INT_EQ(-1, fd);

    fixture_cleanup(&fx);
}

static void test_rejects_null_empty_and_overlong_arguments(void)
{
    int fd = 5;
    char too_long[PATH_MAX_TEST];

    memset(too_long, 'a', sizeof too_long - 1);
    too_long[0] = '/';
    too_long[sizeof too_long - 1] = '\0';

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_dir_open(NULL, &fd));
    CHECK_INT_EQ(-1, fd);
    fd = 5;
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_dir_open("", &fd));
    CHECK_INT_EQ(-1, fd);
    fd = 5;
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_dir_open(too_long, &fd));
    CHECK_INT_EQ(-1, fd);
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_dir_open("/tmp", NULL));
}

static void test_default_data_dir_is_under_the_users_home_library(void)
{
    char path[PATH_CAP];
    struct passwd *entry = getpwuid(geteuid());
    char expected[PATH_CAP];

    CHECK(entry != NULL);
    snprintf(expected, sizeof expected,
             "%s/Library/Application Support/lf-capcheck", entry->pw_dir);

    CHECK_INT_EQ(LFCC_OK, secure_default_data_dir(path, sizeof path));
    CHECK_STR_EQ(expected, path);
}

static void test_default_data_dir_reports_small_buffer_and_clears_output(void)
{
    char tiny[8] = "junk";

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, secure_default_data_dir(tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_default_data_dir(NULL, 64));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_default_data_dir(tiny, 0));
}

static void test_open_existing_reports_not_found_and_creates_nothing(void)
{
    fixture_t fx;
    int fd = 5;
    struct stat info;

    fixture_init(&fx, "absent");

    CHECK_INT_EQ(LFCC_ERR_NOT_FOUND, secure_dir_open_existing(fx.target, &fd));
    CHECK_INT_EQ(-1, fd);
    CHECK(stat(fx.target, &info) != 0); /* still absent */

    fixture_cleanup(&fx);
}

static void test_open_existing_accepts_a_private_directory(void)
{
    fixture_t fx;
    int fd = -1;

    fixture_init(&fx, "data");
    CHECK(mkdir(fx.target, 0700) == 0);

    CHECK_INT_EQ(LFCC_OK, secure_dir_open_existing(fx.target, &fd));
    CHECK(fd >= 0);

    if (fd >= 0) {
        close(fd);
    }
    fixture_cleanup(&fx);
}

static void test_open_existing_applies_the_same_safety_rules(void)
{
    fixture_t fx;
    int fd = -1;
    int file_fd = 0;

    fixture_init(&fx, "data");
    CHECK(mkdir(fx.target, 0700) == 0);
    CHECK(chmod(fx.target, 0750) == 0);
    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, secure_dir_open_existing(fx.target, &fd));
    CHECK_INT_EQ(-1, fd);
    rmdir(fx.target);

    file_fd = open(fx.target, O_CREAT | O_WRONLY, 0600);
    CHECK(file_fd >= 0);
    close(file_fd);
    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, secure_dir_open_existing(fx.target, &fd));
    unlink(fx.target);

    CHECK(symlink("/tmp", fx.target) == 0);
    CHECK_INT_EQ(LFCC_ERR_UNSAFE_PATH, secure_dir_open_existing(fx.target, &fd));

    fixture_cleanup(&fx);
}

static void test_open_existing_rejects_invalid_arguments(void)
{
    int fd = 5;

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_dir_open_existing(NULL, &fd));
    CHECK_INT_EQ(-1, fd);
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_dir_open_existing("", &fd));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, secure_dir_open_existing("/tmp", NULL));
}

int main(void)
{
    RUN_TEST(test_creates_missing_directory_with_private_mode);
    RUN_TEST(test_creates_usable_directory_even_when_umask_strips_owner_bits);
    RUN_TEST(test_accepts_existing_private_directory);
    RUN_TEST(test_rejects_existing_directory_open_to_group_or_others);
    RUN_TEST(test_rejects_symlink_even_when_it_points_to_a_private_directory);
    RUN_TEST(test_rejects_dangling_symlink);
    RUN_TEST(test_rejects_regular_file_where_directory_is_expected);
    RUN_TEST(test_reports_not_found_when_parent_directory_is_missing);
    RUN_TEST(test_rejects_null_empty_and_overlong_arguments);
    RUN_TEST(test_default_data_dir_is_under_the_users_home_library);
    RUN_TEST(test_default_data_dir_reports_small_buffer_and_clears_output);
    RUN_TEST(test_open_existing_reports_not_found_and_creates_nothing);
    RUN_TEST(test_open_existing_accepts_a_private_directory);
    RUN_TEST(test_open_existing_applies_the_same_safety_rules);
    RUN_TEST(test_open_existing_rejects_invalid_arguments);
    return TESTKIT_RESULT();
}
