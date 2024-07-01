#ifndef GBD_H_
#define GBD_H_

#include <stdbool.h>
#include <stdint.h>

#include "rdram.h"
#include "libgfxd/gfxd.h"

typedef struct
{
    uint32_t text_start;
    gfxd_ucode_t ucode;
} gfx_ucode_registry_t;

typedef struct
{
    bool quiet;
    bool print_vertices;
    bool print_textures;
    bool print_matrices;
    bool print_lights;
    bool print_multi_packet;
    bool hex_color;
    bool q_macros;

    int to_num; // Runs to command number and stops

    bool no_volume_cull; // Forces SPCullDisplayList to always fail
    bool no_depth_cull;  // Forces SPBranchLessZ to always succeed
    bool all_depth_cull; // Forces SPBranchLessZ to always fail
} gbd_options_t;

enum start_location_type
{
    USE_GIVEN_START_ADDR,
    USE_START_ADDR_AT_POINTER
};

struct start_location_info
{
    enum start_location_type type;
    uint32_t start_location;
    uint32_t start_location_ptr;
};

int
analyze_gbi (FILE *print_out, gfx_ucode_registry_t *ucodes, gbd_options_t *opts, rdram_interface_t *rdram,
             const void *rdram_arg, struct start_location_info *start_location);

#endif
