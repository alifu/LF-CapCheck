#include "testkit.h"

#include "cli/cli.h"
#include "util/version.h"

#define OUT_CAP 1024

typedef struct {
    int exit_code;
    char out[OUT_CAP];
    char err[OUT_CAP];
} cli_result_t;

/* Runs cli_run with the given arguments, capturing stdout/stderr text. */
static cli_result_t run_cli(int argc, const char *const argv[])
{
    cli_result_t result = {0};
    FILE *out = tmpfile();
    FILE *err = tmpfile();

    result.exit_code = cli_run(argc, argv, out, err);
    tk_read_all(out, result.out, sizeof result.out);
    tk_read_all(err, result.err, sizeof result.err);
    fclose(out);
    fclose(err);
    return result;
}

static void test_version_flag_prints_name_and_version(void)
{
    const char *const argv[] = {"lf-capcheck", "--version"};

    cli_result_t result = run_cli(2, argv);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_STR_EQ("lf-capcheck " LFCC_VERSION "\n", result.out);
    CHECK_STR_EQ("", result.err);
}

static void test_help_flag_prints_usage(void)
{
    const char *const argv[] = {"lf-capcheck", "--help"};

    cli_result_t result = run_cli(2, argv);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
    CHECK_STR_EQ("", result.err);
}

static void test_short_help_flag_prints_usage(void)
{
    const char *const argv[] = {"lf-capcheck", "-h"};

    cli_result_t result = run_cli(2, argv);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
}

static void test_no_arguments_prints_usage(void)
{
    const char *const argv[] = {"lf-capcheck"};

    cli_result_t result = run_cli(1, argv);

    CHECK_INT_EQ(0, result.exit_code);
    CHECK_CONTAINS(result.out, "Usage: lf-capcheck");
}

static void test_unknown_option_fails_with_exit_code_2(void)
{
    const char *const argv[] = {"lf-capcheck", "--bogus"};

    cli_result_t result = run_cli(2, argv);

    CHECK_INT_EQ(2, result.exit_code);
    CHECK_STR_EQ("", result.out);
    CHECK_CONTAINS(result.err, "unknown option '--bogus'");
    CHECK_CONTAINS(result.err, "--help");
}

int main(void)
{
    RUN_TEST(test_version_flag_prints_name_and_version);
    RUN_TEST(test_help_flag_prints_usage);
    RUN_TEST(test_short_help_flag_prints_usage);
    RUN_TEST(test_no_arguments_prints_usage);
    RUN_TEST(test_unknown_option_fails_with_exit_code_2);
    return TESTKIT_RESULT();
}
