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
    printf("Usage: %s [--print-textures] [--print-vertices] [--print-matrices] <file path> <WORK_DISP start>\n", exec_name);
    return -1;
}

int
main (int argc, char **argv)
{
    const char *file_name = NULL;
    uint32_t start_addr;

    bool print_textures = false;
    bool print_vertices = false;
    bool print_matrices = false;

    if (argc < 3)
        return usage(argv[0]);

    bool got_file_name = false;
    bool got_all_args = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--print-textures") == 0)
            print_textures = true;
        else if (strcmp(argv[i], "--print-vertices") == 0)
            print_vertices = true;
        else if (strcmp(argv[i], "--print-matrices") == 0)
            print_matrices = true;
        else if (got_file_name)
        {
            // start address
            if (sscanf(argv[i], "0x%8x", &start_addr) != 1)
            {
                if (strcmp(argv[i], "AUTO") == 0)
                    start_addr = 0xFFFFFFFF;
                else
                {
                    printf("Bad start address.\n");
                    return -1;
                }
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

    analyze_gbi(file_name, start_addr, print_textures, print_vertices, print_matrices);

    return 0;
}
