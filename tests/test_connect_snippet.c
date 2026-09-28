#include "testkit.h"

#include "ui/connect_snippet.h"
#include "vendor/cJSON.h"

#define SNIPPET_CAP 1024
#define PATH_MAX_TEST 512

/* Parses the snippet as JSON and returns the statusLine.command string in `command`. */
static void extract_command(const char *snippet, char *command, size_t cap)
{
    cJSON *root = cJSON_Parse(snippet);
    const cJSON *status_line = cJSON_GetObjectItemCaseSensitive(root, "statusLine");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(status_line, "type");
    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(status_line, "command");

    command[0] = '\0';
    CHECK(root != NULL);
    CHECK(cJSON_IsString(type) && strcmp(type->valuestring, "command") == 0);
    CHECK(cJSON_IsString(cmd));
    if (cJSON_IsString(cmd)) {
        snprintf(command, cap, "%s", cmd->valuestring);
    }
    cJSON_Delete(root);
}

static void check_command_for(const char *executable, const char *expected_shell_command)
{
    char snippet[SNIPPET_CAP];
    char command[SNIPPET_CAP];

    CHECK_INT_EQ(LFCC_OK, connect_snippet_build(executable, snippet, sizeof snippet));
    extract_command(snippet, command, sizeof command);

    CHECK_STR_EQ(expected_shell_command, command);
}

static void test_builds_the_exact_settings_snippet_for_a_plain_path(void)
{
    char snippet[SNIPPET_CAP];

    CHECK_INT_EQ(LFCC_OK, connect_snippet_build("/opt/homebrew/bin/lf-capcheck", snippet,
                                                sizeof snippet));

    CHECK_STR_EQ("{\n"
                 "  \"statusLine\": {\n"
                 "    \"type\": \"command\",\n"
                 "    \"command\": \"/opt/homebrew/bin/lf-capcheck statusline\"\n"
                 "  }\n"
                 "}\n",
                 snippet);
}

static void test_paths_made_of_safe_characters_are_left_unquoted(void)
{
    check_command_for("/usr/local/bin/lf-capcheck", "/usr/local/bin/lf-capcheck statusline");
    check_command_for("/Users/a.b-c_d/bin/x+y@z:1,2=3%4", "/Users/a.b-c_d/bin/x+y@z:1,2=3%4 statusline");
}

static void test_paths_with_spaces_are_shell_quoted(void)
{
    check_command_for("/Users/John Doe/bin/lf-capcheck", "'/Users/John Doe/bin/lf-capcheck' statusline");
}

static void test_single_quotes_are_escaped_for_the_shell_and_then_for_json(void)
{
    /* shell:  '/Users/o'\''brien/lf'  ->  in JSON the backslash is doubled */
    char snippet[SNIPPET_CAP];

    check_command_for("/Users/o'brien/lf", "'/Users/o'\\''brien/lf' statusline");

    CHECK_INT_EQ(LFCC_OK, connect_snippet_build("/Users/o'brien/lf", snippet, sizeof snippet));
    CHECK_CONTAINS(snippet, "\"command\": \"'/Users/o'\\\\''brien/lf' statusline\"");
}

static void test_shell_metacharacters_are_neutralised_by_quoting(void)
{
    check_command_for("/tmp/$HOME/x", "'/tmp/$HOME/x' statusline");
    check_command_for("/tmp/`id`/x", "'/tmp/`id`/x' statusline");
    check_command_for("/tmp/a;b&c|d", "'/tmp/a;b&c|d' statusline");
    check_command_for("/tmp/(x)*?[y]", "'/tmp/(x)*?[y]' statusline");
    check_command_for("/tmp/a\\b", "'/tmp/a\\b' statusline");
    check_command_for("/tmp/a\"b", "'/tmp/a\"b' statusline");
    check_command_for("/tmp/caf\xc3\xa9/lf", "'/tmp/caf\xc3\xa9/lf' statusline");
}

static void test_the_snippet_is_always_valid_json(void)
{
    static const char *const paths[] = {"/a", "/a b", "/a'b", "/a\"b", "/a\\b", "/a\\\"b'c d"};

    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        char snippet[SNIPPET_CAP];
        cJSON *root = NULL;

        CHECK_INT_EQ(LFCC_OK, connect_snippet_build(paths[i], snippet, sizeof snippet));
        root = cJSON_Parse(snippet);
        CHECK(root != NULL);
        cJSON_Delete(root);
    }
}

/*
 * The real proof: let an actual shell read the command back. Every generated
 * path is made of shell-hostile symbols; the only letters are a, b and x, so
 * even a broken quoting could not run anything harmful (and it runs in an
 * empty directory with an empty environment).
 */
static void random_hostile_path(unsigned *state, char *path, size_t cap)
{
    static const char *const pieces[] = {"/", "a", "b", "x", "0", "1", " ", "'", "\"", "\\", "$",
                                         "`", ";", "&", "|", "<", ">", "(", ")", "*", "?", "[",
                                         "]", "{", "}", "~", "!", "#", "%", "=", ":", ",", ".",
                                         "+", "-", "@", "\xc3\xa9", "$(", "${", "'\\''"};
    size_t count = 1;
    size_t used = 0;

    *state = *state * 1664525u + 1013904223u;
    count = 1 + (*state >> 16) % 24;
    path[0] = '/';
    path[1] = '\0';
    used = 1;
    for (size_t i = 0; i < count; i++) {
        const char *piece = NULL;
        size_t length = 0;

        *state = *state * 1664525u + 1013904223u;
        piece = pieces[(*state >> 16) % (sizeof pieces / sizeof pieces[0])];
        length = strlen(piece);
        if (used + length + 1 >= cap) {
            break;
        }
        memcpy(path + used, piece, length);
        used += length;
        path[used] = '\0';
    }
}

static void test_a_real_shell_reads_every_quoted_path_back_exactly(void)
{
    char sandbox[PATH_MAX_TEST];
    char script_path[PATH_MAX_TEST + 16];
    char run_command[PATH_MAX_TEST * 2];
    unsigned state = 12345u;

    CHECK(tk_make_temp_dir(sandbox, sizeof sandbox) == 0);
    snprintf(script_path, sizeof script_path, "%s/run.sh", sandbox);
    snprintf(run_command, sizeof run_command, "cd '%s' && env -i /bin/sh ./run.sh 2>&1", sandbox);

    for (int round = 0; round < 300; round++) {
        char path[128];
        char snippet[SNIPPET_CAP];
        char command[SNIPPET_CAP];
        char expected[256];
        char output[512] = "";
        FILE *script = NULL;
        FILE *pipe_out = NULL;
        size_t got = 0;

        random_hostile_path(&state, path, sizeof path);
        CHECK_INT_EQ(LFCC_OK, connect_snippet_build(path, snippet, sizeof snippet));
        extract_command(snippet, command, sizeof command);

        /* printf prints each shell word of the command on its own line. */
        script = fopen(script_path, "w");
        CHECK(script != NULL);
        fprintf(script, "printf '%%s\\n' %s\n", command);
        fclose(script);

        pipe_out = popen(run_command, "r");
        CHECK(pipe_out != NULL);
        got = fread(output, 1, sizeof output - 1, pipe_out);
        output[got] = '\0';
        CHECK(pclose(pipe_out) == 0);

        snprintf(expected, sizeof expected, "%s\nstatusline\n", path);
        if (strcmp(expected, output) != 0) {
            fprintf(stderr, "    shell disagreed for path [%s]: got [%s]\n", path, output);
        }
        CHECK_STR_EQ(expected, output);
    }

    tk_remove_dir(sandbox);
}

static void test_rejects_paths_that_cannot_be_written_safely(void)
{
    char snippet[SNIPPET_CAP];

    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build(NULL, snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("relative/lf", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("./lf", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/tmp/a\nb", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/tmp/a\tb", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/tmp/a\x1b[31m", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/tmp/a\x7f", snippet, sizeof snippet));
    /* Invalid UTF-8 would make the pasted JSON invalid; a C1 control (here CSI) can drive some terminals. */
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/tmp/a\xff", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/tmp/a\xc3", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/tmp/a\xc2\x9b", snippet, sizeof snippet));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/opt/lf", NULL, SNIPPET_CAP));
    CHECK_INT_EQ(LFCC_ERR_INVALID_ARG, connect_snippet_build("/opt/lf", snippet, 0));
    CHECK_STR_EQ("", snippet);
}

static void test_reports_a_buffer_that_is_too_small_and_clears_it(void)
{
    char snippet[SNIPPET_CAP];
    char tiny[20] = "junk";
    char exact[SNIPPET_CAP];
    size_t needed = 0;

    CHECK_INT_EQ(LFCC_OK, connect_snippet_build("/opt/lf", snippet, sizeof snippet));
    needed = strlen(snippet) + 1;

    CHECK_INT_EQ(LFCC_ERR_CAPACITY, connect_snippet_build("/opt/lf", tiny, sizeof tiny));
    CHECK_STR_EQ("", tiny);
    CHECK_INT_EQ(LFCC_ERR_CAPACITY, connect_snippet_build("/opt/lf", exact, needed - 1));
    CHECK_INT_EQ(LFCC_OK, connect_snippet_build("/opt/lf", exact, needed));
}

int main(void)
{
    RUN_TEST(test_builds_the_exact_settings_snippet_for_a_plain_path);
    RUN_TEST(test_paths_made_of_safe_characters_are_left_unquoted);
    RUN_TEST(test_paths_with_spaces_are_shell_quoted);
    RUN_TEST(test_single_quotes_are_escaped_for_the_shell_and_then_for_json);
    RUN_TEST(test_shell_metacharacters_are_neutralised_by_quoting);
    RUN_TEST(test_the_snippet_is_always_valid_json);
    RUN_TEST(test_a_real_shell_reads_every_quoted_path_back_exactly);
    RUN_TEST(test_rejects_paths_that_cannot_be_written_safely);
    RUN_TEST(test_reports_a_buffer_that_is_too_small_and_clears_it);
    return TESTKIT_RESULT();
}
