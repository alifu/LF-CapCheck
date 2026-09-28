#include <stdio.h>

#include "cli/cli.h"

int main(int argc, char *argv[])
{
    return cli_run(argc, (const char *const *)argv, stdout, stderr);
}
