#ifndef RDRAM_H_
#define RDRAM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// clang-format off
typedef struct {
    int    (*close)     (void);
    int    (*open)      (const void *arg);
    long   (*pos)       (void);
    bool   (*addr_valid)(uint32_t addr);
    size_t (*read)      (void *buf, size_t elem_size, size_t elem_count);
    bool   (*seek)      (uint32_t addr);
    bool   (*read_at)   (void *buf, uint32_t addr, size_t size);
} rdram_interface_t;
// clang-format on

extern rdram_interface_t rdram_interface_file;

#endif
