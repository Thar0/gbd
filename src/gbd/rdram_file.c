#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "libgbd/rdram.h"

/**
 *  RDRAM File Implementation
 */

static long
rdram_file_pos(void);

static bool
rdram_file_seek(uint32_t addr);

static FILE *rdram_file_file;
static long  rdram_file_size;

static int
rdram_file_close(void)
{
    return fclose(rdram_file_file);
}

static int
rdram_file_open(const void *arg)
{
    rdram_file_file = fopen((const char *)arg, "rb");

    int ret = -(rdram_file_file == NULL); // 0 = success, -1 = could not open file
    if (ret == 0) {
        if (fseek(rdram_file_file, 0, SEEK_END))
            return -2; // -2 = could not ascertain size
        rdram_file_size = rdram_file_pos();
        rdram_file_seek(0);
    }
    return ret;
}

static long
rdram_file_pos(void)
{
    return ftell(rdram_file_file);
}

static bool
rdram_file_addr_valid(uint32_t addr)
{
    return addr < (unsigned long)rdram_file_size;
}

static size_t
rdram_file_read(void *buf, size_t elem_size, size_t elem_count)
{
    return fread(buf, elem_size, elem_count, rdram_file_file);
}

/**
 * Returns true if seek to `addr` was successful.
 */
static bool
rdram_file_seek(uint32_t addr)
{
    return rdram_file_addr_valid(addr) && (fseek(rdram_file_file, addr, SEEK_SET) == 0);
}

/**
 * Returns true if read of `size` bytes at `addr` was successful.
 */
static bool
rdram_file_read_at(void *buf, uint32_t addr, size_t size)
{
    return rdram_file_seek(addr) && (rdram_file_read(buf, size, 1) == 1);
}

/**
 *  RDRAM File Interface
 */

rdram_interface_t rdram_interface_file = {
    rdram_file_close, rdram_file_open, rdram_file_pos,     rdram_file_addr_valid,
    rdram_file_read,  rdram_file_seek, rdram_file_read_at,
};
