#include <stdio.h>

#include "cli/cli.h"

int main(int argc, char *argv[])
{
    const cli_io_t io = {stdin, stdout, stderr, NULL, 0};

    return cli_run(argc, (const char *const *)argv, &io);
}
