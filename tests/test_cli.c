#include "testkit.h"

#include "cli/cli.h"
#include "util/secure_path.h"
#include "util/version.h"
#include "store/snapshot_store.h"

#include <unistd.h>

#define OUT_CAP 1024
#define PATH_CAP 512
#define FIXTURE_CAP 16384
#define NOW 1738400000

typedef struct {
    int exit_code;
    char out[OUT_CAP];
    char err[OUT_CAP];
} cli_result_t;

/* Runs cli_run with the given arguments and stdin text, capturing stdout/stderr. */
static cli_result_t run_cli(int argc, const char *const argv[], const char *input,
                            const char *data_dir)
{
    cli_result_t result = {0};
    FILE *in = tmpfile();
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    cli_io_t io = {in, out, err, data_dir, NOW};

    if (input != NULL) {
        fputs(input, in);
        rewind(in);
    }
    result.exit_code = cli_run(argc, argv, &io);
    tk_read_all(out, result.out, sizeof result.out);
    tk_read_all(err, result.err, sizeof result.err);
    fclose(in);
    fclose(out);
    fclose(err);
    return result;
}

static void test_version_flag_prints_name_and_version(void)
{
    const char *const argv[] = {"lf-capcheck", "--version"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("lf-capcheck " LFCC_VERSION "\n", result.out);
    CHECK_STR_EQ("", result.err);
}

static void test_help_flag_prints_usage_including_the_statusline_command(void)
{
    const char *const argv[] = {"lf-capcheck", "--help"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
    CHECK_CONTAINS(result.out, "statusline");
    CHECK_STR_EQ("", result.err);
}

static void test_short_help_flag_prints_usage(void)
{
    const char *const argv[] = {"lf-capcheck", "-h"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
}

static void test_no_arguments_prints_usage(void)
{
    const char *const argv[] = {"lf-capcheck"};

    cli_result_t result = run_cli(1, argv, NULL, NULL);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
}

static void test_unknown_option_fails_with_exit_code_2(void)
{
    const char *const argv[] = {"lf-capcheck", "--bogus"};

    cli_result_t result = run_cli(2, argv, NULL, NULL);

    CHECK_INT_EQ(2, result.exit_code);
    CHECK_STR_EQ("", result.out);
    CHECK_CONTAINS(result.err, "unknown option '--bogus'");
    CHECK_CONTAINS(result.err, "--help");
}

static void test_statusline_command_reads_stdin_saves_and_prints(void)
{
    const char *const argv[] = {"lf-capcheck", "statusline"};
    char json[FIXTURE_CAP];
    char data_dir[PATH_CAP];
    long length = tk_read_file("tests/fixtures/statusline_full.json", json, sizeof json);
    snap_record_t saved = {0};
    cli_result_t result;
    int fd = -1;

    CHECK(length > 0);
    CHECK(tk_make_temp_dir(data_dir, sizeof data_dir) == 0);

    result = run_cli(2, argv, json, data_dir);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "5h 77% left");
    CHECK_STR_EQ("", result.err);
    CHECK_INT_EQ(LFCC_OK, secure_dir_open(data_dir, &fd));
    CHECK_INT_EQ(LFCC_OK, snap_read(fd, &saved));
    CHECK_INT_EQ(NOW, saved.as_of);

    close(fd);
    tk_remove_dir(data_dir);
}

int main(void)
{
    RUN_TEST(test_version_flag_prints_name_and_version);
    RUN_TEST(test_help_flag_prints_usage_including_the_statusline_command);
    RUN_TEST(test_short_help_flag_prints_usage);
    RUN_TEST(test_no_arguments_prints_usage);
    RUN_TEST(test_unknown_option_fails_with_exit_code_2);
    RUN_TEST(test_statusline_command_reads_stdin_saves_and_prints);
    return TESTKIT_RESULT();
}
