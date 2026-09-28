#include <stdio.h>

#include "cli/cli.h"
#include "ui/terminal.h"

int main(int argc, char *argv[])
{
    const cli_io_t io = {stdin, stdout, stderr, NULL, 0, NULL, terminal_style_detect(stdout)};

    return cli_run(argc, (const char *const *)argv, &io);
}
