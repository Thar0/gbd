#ifndef GFX_H_
#define GFX_H_

#include "libgfxd/gfxd.h"

#define F3DEX_GBI_2
#define F3DEX_GBI_3
#define F3DEX_GBI_PL
#include "libgfxd/gbi.h"

#define MDMASK(md) ((((uint32_t)1 << G_MDSIZ_##md) - 1) << G_MDSFT_##md)

#define CVG_DST_MASK (gI_(0b11) << (G_MDSFT_RENDERMODE + 5))

typedef struct {
    float mf[4][4];
} MtxF;

#endif
