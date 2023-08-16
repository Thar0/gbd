#ifndef GBD_H_
#define GBD_H_

#include <stdbool.h>
#include <stdint.h>

enum start_addr_strategy
{
    USE_GIVEN_START_ADDR,
    USE_START_ADDR_AT_GIVEN_LOCATION,
    USE_DEFAULT_START_ADDR
};

struct analyze_options
{
    enum start_addr_strategy start_addr_strat;
    uint32_t start_addr;
    uint32_t start_addr_location;
    bool print_textures;
    bool print_vertices;
    bool print_matrices;
};

int
analyze_gbi (const char *file_name, struct analyze_options options);

#endif
