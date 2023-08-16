#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "gbd.h"

static bool
file_exists (const char *filename) {
    struct stat buffer;

    return stat(filename, &buffer) == 0;
}

static int
usage (char *exec_name)
{
    printf("Usage: %s [--print-textures] [--print-vertices] [--print-matrices] <file path> <WORK_DISP start | *ptr to WORK_DISP start>\n", exec_name);
    return -1;
}

int
main (int argc, char **argv)
{
    const char *file_name = NULL;

    struct analyze_options options = {
        .start_addr_strat = USE_DEFAULT_START_ADDR,
        .print_textures = false,
        .print_vertices = false,
        .print_matrices = false,
    };

    if (argc < 3)
        return usage(argv[0]);

    bool got_file_name = false;
    bool got_all_args = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--print-textures") == 0)
            options.print_textures = true;
        else if (strcmp(argv[i], "--print-vertices") == 0)
            options.print_vertices = true;
        else if (strcmp(argv[i], "--print-matrices") == 0)
            options.print_matrices = true;
        else if (got_file_name)
        {
            char* addr_str = argv[i];
            if (addr_str[0] == '*')
            {
                options.start_addr_strat = USE_START_ADDR_AT_GIVEN_LOCATION;
                addr_str += 1;
            }
            else
            {
                options.start_addr_strat = USE_GIVEN_START_ADDR;
            }
            uint32_t addr;
            if (sscanf(addr_str, "0x%8x", &addr) != 1)
            {
                if (strcmp(addr_str, "AUTO") == 0)
                    options.start_addr_strat = USE_DEFAULT_START_ADDR;
                else
                {
                    printf("Bad start address.\n");
                    return -1;
                }
            }
            if (options.start_addr_strat == USE_GIVEN_START_ADDR)
            {
                options.start_addr = addr;
            }
            else if (options.start_addr_strat == USE_START_ADDR_AT_GIVEN_LOCATION)
            {
                options.start_addr_location = addr;
            }
            got_all_args = true;
        }
        else
        {
            // file name
            file_name = argv[i];
            if (!file_exists(file_name))
            {
                printf("File %s does not exist.\n", file_name);
                return -1;
            }
            got_file_name = true;
        }
    }

    if (!got_all_args)
        return usage(argv[0]);

    analyze_gbi(file_name, options);

    return 0;
}
