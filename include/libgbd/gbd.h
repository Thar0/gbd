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
    int line;
} gbd_options_t;

int
analyze_gbi (FILE *print_out, gfx_ucode_registry_t *ucodes, gbd_options_t *opts, rdram_interface_t *rdram,
             const void *rdram_arg, uint32_t start_addr, uint32_t auto_start_ptr_addr);

#endif
