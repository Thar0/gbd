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

int
main (int argc, char **argv)
{
    const char *file_name;
    uint32_t start_addr;

    if (argc < 3)
    {
        printf("Usage: %s [--print-textures] [--print-vertices] <file path> <WORK_DISP start>\n", argv[0]);
        return -1;
    }

    file_name = argv[1];

    if (!file_exists(file_name))
    {
        printf("File %s does not exist.\n", file_name);
        return -1;
    }

    if (sscanf(argv[2], "0x%8x", &start_addr) != 1)
    {
        if (strcmp(argv[2], "AUTO") == 0)
            start_addr = 0xFFFFFFFF;
        else
        {
            printf("Bad start address.\n");
            return -1;
        }
    }

    analyze_gbi(file_name, start_addr);

    return 0;
}
