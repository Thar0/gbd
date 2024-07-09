#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "libgbd/gbd.h"

#define strequ(s1, s2) (strcmp((s1), (s2)) == 0)

static bool
file_exists(const char *filename)
{
    struct stat buffer;

    return stat(filename, &buffer) == 0;
}

static int
usage(char *exec_name)
{
    printf("Usage: %s "
           "[--print-textures] "
           "[--print-vertices] "
           "[--print-matrices] "
           "[--print-lights] "
           "[--to-line <n>] "
           "[--quiet] "
           "<file path> "
           "<WORK_DISP start | *<pointer to WORK_DISP start>>"
           "\n",
           exec_name);
    return -1;
}

int
main(int argc, char **argv)
{
    /* MQ debug ROM ucodes, TODO make ucodes externally configurable */
    gfx_ucode_registry_t ucodes[] = {
        {0x80155F50,  gfxd_f3dex2},
        { 0x80113070, gfxd_s2dex2},
        { 0,          NULL       },
    };
    // default AUTO start pointer (MQ debug sPrevTaskWorkBuffer)
#define WORK_DISP_PTR 0x8012D260

    gbd_options_t opts = {
        .q_macros = true,
    };

    const char                *file_name = NULL;
    struct start_location_info start_location;

    if (argc < 3)
        return usage(argv[0]);

    bool got_file_name = false;
    bool got_all_args  = false;

    for (int i = 1; i < argc; i++) {
        if (strequ(argv[i], "--print-textures"))
            opts.print_textures = true;
        else if (strequ(argv[i], "--print-vertices"))
            opts.print_vertices = true;
        else if (strequ(argv[i], "--print-matrices"))
            opts.print_matrices = true;
        else if (strequ(argv[i], "--print-lights"))
            opts.print_lights = true;
        else if (strequ(argv[i], "--print-multi-packet"))
            opts.print_multi_packet = true;
        else if (strequ(argv[i], "--quiet"))
            opts.quiet = true;
        // TODO formatting options:
        //  - display list stack indentation
        //  - hide empty display lists
        else if (strequ(argv[i], "--to-num")) {
            if (i + 1 >= argc || sscanf(argv[i + 1], "%d", &opts.to_num) != 1)
                return usage(argv[0]);
            i++;
        } else {
            // required args

            if (got_file_name) {
                // start address

                if (strequ(argv[i], "AUTO")) {
                    start_location.type               = USE_START_ADDR_AT_POINTER;
                    start_location.start_location_ptr = WORK_DISP_PTR;
                } else {
                    char     *addr_str;
                    uint32_t *addrp;

                    if (argv[i][0] == '*') {
                        start_location.type = USE_START_ADDR_AT_POINTER;

                        addr_str = &argv[i][1];
                        addrp    = &start_location.start_location_ptr;
                    } else {
                        start_location.type = USE_GIVEN_START_ADDR;

                        addr_str = argv[i];
                        addrp    = &start_location.start_location;
                    }
                    if (sscanf(addr_str, "0x%8x", addrp) != 1) {
                        printf("Bad start address.\n");
                        return -1;
                    }
                }
                got_all_args = true;
            } else {
                // file name
                file_name = argv[i];
                if (!file_exists(file_name)) {
                    printf("File %s does not exist.\n", file_name);
                    return -1;
                }
                got_file_name = true;
            }
        }
    }

    if (!got_all_args)
        return usage(argv[0]);
    analyze_gbi(stdout, ucodes, &opts, &rdram_interface_file, file_name, &start_location);

    return 0;
}
