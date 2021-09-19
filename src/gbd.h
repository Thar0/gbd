#ifndef GBD_H_
#define GBD_H_

#include <stdint.h>

int
analyze_gbi (const char *file_name, uint32_t start_addr, bool print_textures, bool print_vertices, bool print_matrices);

#endif
