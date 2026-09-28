#include "testkit.h"

#include "ui/executable_path.h"

#define PATH_CAP 512

static void check_maps_to(const char *input, const char *expected)
{
    char out[PATH_CAP];

    CHECK_INT_EQ(LFCC_OK, executable_stable_path(input, out, sizeof out));
    CHECK_STR_EQ(expected, out);
}

static void test_homebrew_cellar_paths_map_to_the_stable_prefix_bin(void)
{
    check_maps_to("/opt/homebrew/Cellar/lf-capcheck/0.1.0/bin/lf-capcheck",
                  "/opt/homebrew/bin/lf-capcheck");
    check_maps_to("/usr/local/Cellar/lf-capcheck/0.1.0_1/bin/lf-capcheck",
                  "/usr/local/bin/lf-capcheck");
    check_maps_to("/home/linuxbrew/.linuxbrew/Cellar/lf-capcheck/1.2.3/bin/lf-capcheck",
                  "/home/linuxbrew/.linuxbrew/bin/lf-capcheck");
}

static void test_other_paths_are_returned_unchanged(void)
{
    check_maps_to("/Users/me/project/build/lf-capcheck", "/Users/me/project/build/lf-capcheck");
    check_maps_to("/usr/local/bin/lf-capcheck", "/usr/local/bin/lf-capcheck");
    /* Looks similar but is not <prefix>/Cellar/<formula>/<version>/bin/<name>. */
    check_maps_to("/opt/homebrew/Cellar/lf-capcheck/0.1.0/libexec/lf-capcheck",
                  "/opt/homebrew/Cellar/lf-capcheck/0.1.0/libexec/lf-capcheck");
    check_maps_to("/opt/homebrew/Cellar/lf-capcheck/bin/lf-capcheck",
                  "/opt/homebrew/Cellar/lf-capcheck/bin/lf-capcheck");
    check_maps_to("/opt/homebrew/Cellar/lf-capcheck/0.1.0/bin/sub/lf-capcheck",
                  "/opt/homebrew/Cellar/lf-capcheck/0.1.0/bin/sub/lf-capcheck");
    check_maps_to("/Users/me/Cellar-notes/lf-capcheck", "/Users/me/Cellar-notes/lf-capcheck");
    /* No prefix in front of /Cellar: mapping would invent "/bin/lf-capcheck". */
    check_maps_to("/Cellar/x/1/bin/lf-capcheck", "/Cellar/x/1/bin/lf-capcheck");
}

static void test_stable_path_rejects_bad_arguments_and_small_buffers(void)
{
    char out[PATH_CAP];
    char tiny[8] = "junk";

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, executable_stable_path(NULL, out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, executable_stable_path("", out, sizeof out));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, executable_stable_path("/a", NULL, 10));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, executable_stable_path("/a", out, 0));
    CHECK_INT_EQ(LFCC_ERR_CAPACITY, executable_stable_path("/Users/me/project/build/lf-capcheck", tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);
    CHECK_INT_EQ(LFCC_ERR_CAPACITY,
                 executable_stable_path("/opt/homebrew/Cellar/lf-capcheck/0.1.0/bin/lf-capcheck", tiny, sizeof tiny));
}

static void test_current_path_is_absolute_and_names_this_program(void)
{
    char path[PATH_CAP];

    CHECK_INT_EQ(LFCC_OK, executable_current_path(path, sizeof path));

    CHECK(path[0] == '/');
    CHECK_CONTAINS(path, "test_executable_path");
}

static void test_current_path_reports_a_small_buffer(void)
{
    char tiny[4] = "abc";

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, executable_current_path(tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, executable_current_path(NULL, 10));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, executable_current_path(tiny, 0));
}

int main(void)
{
    RUN_TEST(test_homebrew_cellar_paths_map_to_the_stable_prefix_bin);
    RUN_TEST(test_other_paths_are_returned_unchanged);
    RUN_TEST(test_stable_path_rejects_bad_arguments_and_small_buffers);
    RUN_TEST(test_current_path_is_absolute_and_names_this_program);
    RUN_TEST(test_current_path_reports_a_small_buffer);
    return TESTKIT_RESULT();
}
