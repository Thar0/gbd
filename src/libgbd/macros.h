#ifndef MACROS_H_
#define MACROS_H_

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define FALLTHROUGH __attribute__((fallthrough))

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define SHIFTL(v, n, s) ((uint32_t)(((uint32_t)(v) & ((((uint32_t)(1)) << (n)) - 1)) << (s)))

#define SHIFTR(v, n, s) ((uint32_t)(((uint32_t)(v) >> (s)) & ((((uint32_t)(1)) << (n)) - 1)))

// TODO make more portable
#define BSWAP16(x) __builtin_bswap16(x)

#define MAYBE_BSWAP16(v, c) ((c) ? BSWAP16(v) : (v))

// TODO make more portable
#define BSWAP32(x) __builtin_bswap32(x)

#define MAYBE_BSWAP32(v, c) ((c) ? BSWAP32(v) : (v))

#define FMT_SIZ(fmt, siz) (((fmt) << 2) | (siz))

#define F3DZEX_CONST(name) \
    {                      \
        name, #name        \
    }

struct F3DZEX_const {
    uint32_t value;
    const char *name;
};

#define F3DZEX_MTX_PARAM(set_name, unset_name)             \
    {                                                      \
        (set_name) & ~(unset_name), #set_name, #unset_name \
    }

#endif
