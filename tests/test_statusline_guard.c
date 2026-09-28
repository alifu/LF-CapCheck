#include "testkit.h"

#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cli/statusline.h"

#define NOW 1738400000
#define PATH_CAP 512
#define CHILD_FAILED 99
#define CHILD_SIGNAL_NOT_RESTORED 97
#define VALID_PAYLOAD                                                                        \
    "{\"rate_limits\":{\"five_hour\":{\"used_percentage\":20,\"resets_at\":1738425600},"     \
    "\"seven_day\":{\"used_percentage\":30,\"resets_at\":1738857600}}}"

static double elapsed_since(const struct timespec *start)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) + (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

static bool signal_is_default(int signal_number)
{
    struct sigaction current;

    return sigaction(signal_number, NULL, &current) == 0 && current.sa_handler == SIG_DFL;
}

static void test_gives_up_quietly_when_the_input_never_ends(void)
{
    int pipe_fds[2];
    struct timespec start;
    pid_t child = 0;
    int wait_status = 0;
    double seconds = 0.0;

    CHECK(pipe(pipe_fds) == 0);
    clock_gettime(CLOCK_MONOTONIC, &start);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        /* The write end stays open in this process: the reader can never see end of input. */
        FILE *in = fdopen(pipe_fds[0], "r");
        cli_io_t io = {in, tmpfile(), tmpfile(), "/nonexistent", NOW, NULL, {80, false, false}, 1};

        statusline_run(&io);
        _exit(CHILD_FAILED); /* it must have stopped waiting via the watchdog instead */
    }
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    CHECK(waitpid(child, &wait_status, 0) == child);
    seconds = elapsed_since(&start);

    CHECK(WIFEXITED(wait_status));
    CHECK_INT_EQ(0, WEXITSTATUS(wait_status));
    CHECK(seconds >= 0.9); /* it really waited for the timeout ... */
    CHECK(seconds < 4.0);  /* ... and did not hang */
}

static void test_no_alarm_or_signal_handler_is_left_behind_after_a_normal_run(void)
{
    char data_dir[PATH_CAP];
    FILE *in = tmpfile();
    cli_io_t io = {in, tmpfile(), tmpfile(), data_dir, NOW, NULL, {80, false, false}, 5};

    CHECK(tk_make_temp_dir(data_dir, sizeof data_dir) == 0);
    fputs(VALID_PAYLOAD, in);
    rewind(in);

    CHECK_INT_EQ(0, statusline_run(&io));

    CHECK_INT_EQ(0, (int)alarm(0)); /* nothing was still pending */
    CHECK(signal_is_default(SIGALRM));
    CHECK(signal_is_default(SIGPIPE));

    tk_remove_dir(data_dir);
}

static void test_a_closed_output_pipe_does_not_kill_it(void)
{
    char data_dir[PATH_CAP];
    int pipe_fds[2];
    pid_t child = 0;
    int wait_status = 0;

    CHECK(tk_make_temp_dir(data_dir, sizeof data_dir) == 0);
    CHECK(pipe(pipe_fds) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        FILE *in = tmpfile();
        FILE *out = NULL;
        cli_io_t io = {in, NULL, tmpfile(), data_dir, NOW, NULL, {80, false, false}, 5};
        int code = 0;

        close(pipe_fds[0]); /* nobody reads the other end any more */
        out = fdopen(pipe_fds[1], "w");
        io.out = out;
        fputs(VALID_PAYLOAD, in);
        rewind(in);

        code = statusline_run(&io); /* would die from SIGPIPE if the signal were not ignored */
        _exit(code != 0 ? CHILD_FAILED : (signal_is_default(SIGPIPE) ? 0 : CHILD_SIGNAL_NOT_RESTORED));
    }
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    CHECK(waitpid(child, &wait_status, 0) == child);
    CHECK(WIFEXITED(wait_status)); /* not killed by a signal */
    CHECK_INT_EQ(0, WEXITSTATUS(wait_status));

    tk_remove_dir(data_dir);
}

int main(void)
{
    RUN_TEST(test_gives_up_quietly_when_the_input_never_ends);
    RUN_TEST(test_no_alarm_or_signal_handler_is_left_behind_after_a_normal_run);
    RUN_TEST(test_a_closed_output_pipe_does_not_kill_it);
    return TESTKIT_RESULT();
}
