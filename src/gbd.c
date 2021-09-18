#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include "libgfxd/gfxd.h"

#define F3DEX_GBI_2
#include "libgfxd/gbi.h"
// Additional geometry mode
#define G_LIGHTING_POSITIONAL   (gI_(0b1) << 22)

#define _SHIFTL(v, n, s)    \
    ((uint32_t)(((uint32_t)(v) & ((((uint32_t)(1)) << (n)) - 1)) << (s)))

#define _SHIFTR(v, n, s)    \
    ((uint32_t)(((uint32_t)(v) >> (s)) & ((((uint32_t)(1)) << (n)) - 1)))

typedef struct
{
    float mf[4][4];
} MtxF;

#include "gbd.h"

#define F3DZEX_CONST(name) \
    { name, #name }

#define F3DZEX_MTX_PARAM(set_name, unset_name) \
    { (set_name) & ~(unset_name), #set_name, #unset_name }

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

#define KSEG_MASK (0b111 << 29)

typedef int (*print_fn)(const char *fmt, ...);

/**************************************************************************
 *  Global State
 */

static long gfx_addr;
static long n_gfx;

static uint32_t segment_table[16];

static uint32_t othermode_hi;
static uint32_t othermode_lo;

static uint32_t geometry_mode;

static uint32_t combine_mode_hi;
static uint32_t combine_mode_lo;

// TODO emulate the S2DEX2 status register

static bool task_done = false;
static bool pipeline_crashed = false;

static bool pipesync_required = false;
static bool loadsync_required = false;
static bool tilesync_required = false;

static int last_macro_size = sizeof(Gfx);

static struct
{
    uint32_t tlut;
    int idx;
    int count;
    int type;
} last_tlut;

static struct
{
    uint32_t timg;
    int fmt;
    int siz;
    int width;
    int height;
    int pal;
    uint32_t tlut;
    int tlut_count;
    int tlut_idx;
    int tlut_type;
} last_timg;

static struct
{
    uint32_t vtx;
    int32_t num;
} last_vtx;

static struct
{
    uint32_t mtx;
    int params;
} last_mtx;

static struct
{
    uint32_t file;
    int line;
} last_open_disps;

static gfxd_ucode_t next_ucode;

/**************************************************************************
 *  RDRAM Interface
 */

static FILE *rdram_file;

int rdram_close(void)
{
    return fclose(rdram_file);
}

int rdram_open(const void *arg)
{
    rdram_file = fopen((const char *)arg, "rb");
    return -(rdram_file == NULL);
}

long rdram_pos(void)
{
    return ftell(rdram_file);
}

size_t rdram_read(void *buf, size_t elem_size, size_t elem_count)
{
    // printf("[RI] READ: pos: 0x%08lX len: 0x%X\n", rdram_pos(), len);
    return fread(buf, elem_size, elem_count, rdram_file);
}

int rdram_seek(uint32_t addr)
{
    return fseek(rdram_file, addr, SEEK_SET);
}

/**************************************************************************
 *  Display List Stack
 */

uint32_t dl_stack[18];
int dl_stack_top = -1;

static uint32_t dl_stack_peek(void)
{
    if (dl_stack_top == -1)
        return 0; // peek failed, stack empty
    return dl_stack[dl_stack_top];
}

static uint32_t dl_stack_pop(void)
{
    if (dl_stack_top == -1)
        return 0; // pop failed, stack empty
    return dl_stack[dl_stack_top--];
}

static int dl_stack_push(uint32_t dl)
{
    if (dl_stack_top != ARRAY_COUNT(dl_stack))
        dl_stack[++dl_stack_top] = dl;
    else
        return 0; // push failed, stack full
    return 1;
}

/**************************************************************************
 *  Matrix Conversion
 */

#define MAYBE_BSWAP16(v, c) \
    ((c) ? __builtin_bswap16((v)) : (v))

static inline void
f_to_qs1616(int16_t *int_out, uint16_t *frac_out, float f)
{
    qs1616_t q = qs1616(f);

    *int_out = (int16_t)(q >> 16);
    *frac_out = (uint16_t)(q & 0xFFFF);
}

static inline float
qs1616_to_f(int16_t int_part, uint16_t frac_part)
{
    return ((int_part << 16) | (frac_part)) / (float)0x10000;
}

static void
mtxf_to_mtx (Mtx *mtx, MtxF *mf, bool swap)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
        {
            int16_t i;
            uint16_t f;

            f_to_qs1616(&i, &f, mf->mf[i][j]);

            mtx->i[i + 4 * j] = MAYBE_BSWAP16(i, swap);
            mtx->f[i + 4 * j] = MAYBE_BSWAP16(f, swap);
        }
}

static void
mtx_to_mtxf (MtxF *mf, Mtx *mtx, bool swap)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) 
        {
            mtx->i[i + 4 * j] = MAYBE_BSWAP16(mtx->i[i + 4 * j], swap);
            mtx->f[i + 4 * j] = MAYBE_BSWAP16(mtx->f[i + 4 * j], swap);

            mf->mf[i][j] = qs1616_to_f(mtx->i[i + 4 * j], mtx->f[i + 4 * j]);
        }
}

/**************************************************************************
 *  Matrix Stack
 * 
 * The F3DZEX matrix stack is stored in RDRAM in the OSTask DRAM Stack
 * region. The microcode maintains a pointer to the current position in
 * the DRAM Stack of the most recently placed Mtx.
 */



/**************************************************************************
 *  Address Conversion and Checking
 */

static uint32_t
physical_to_kseg0 (uint32_t addr)
{
    return addr + 0x80000000;
}

static uint32_t
segmented_to_physical (uint32_t addr)
{
    // mask out any kseg bits (will not affect segmented addresses)
    addr &= ~KSEG_MASK;

    // segment calculation
    return segment_table[(addr << 4) >> 28] + (addr & 0x00FFFFFF);
}

static uint32_t
segmented_to_kseg0 (uint32_t addr)
{
    return physical_to_kseg0(segmented_to_physical(addr));
}

static bool
addr_in_rdram (uint32_t addr)
{
    addr = segmented_to_physical(addr);

    return (0 <= addr) && (addr < 0x800000);
}

/**************************************************************************
 *  Texture Engine Emulator
 */

typedef struct
{
    bool on;
    uint16_t scaleS;
    uint16_t scaleT;
    int level;
    qu102_t uls;
    qu102_t ult;
    qu102_t lrs;
    qu102_t lrt;
    int fmt;
    int siz;
    int line;
    int tmem;
    int palette;
    int cmt;
    int cmS;
    int masks;
    int maskt;
    int shifts;
    int shiftt;
} tile_descriptor_t;

tile_descriptor_t tiles[8];
uint8_t tmem[0x1000];

static void 
emu_settimg()
{
    ;
}

static void 
emu_settile()
{
    ;
}

static void 
emu_loadblock()
{
    ;
}

static void 
emu_settilesize()
{
    ;
}

/**************************************************************************
 *  Memory Interface
 */

typedef struct
{
    uint8_t m; // min prim color value
    uint8_t l; // lod factor for prim color interp
    uint32_t prim_col;
    uint32_t env_col;
    uint32_t blend_col;
} rdp_mi_t;

rdp_mi_t mem_if;

static void
emu_setprimcolor (uint8_t m, uint8_t l, uint32_t prim_col)
{
    mem_if.m = m;
    mem_if.l = l;
    mem_if.prim_col = prim_col;
}

static void
emu_setenvcolor (uint32_t env_col)
{
    mem_if.env_col = env_col;
}

/**************************************************************************
 *  Printers
 */

#define PRINT_PX(r, g, b) \
    printf("\x1b[48;2;%d;%d;%dm" "\x1b[38;2;%d;%d;%dm" "\u2584\u2584", r, g, b, r, g, b)

#define FMT_SIZ(fmt, siz) \
    (((fmt) << 2) | (siz))

#define CVT_PX(c, sft, mask) \
    ((((c) >> (sft)) & (mask)) * (255/(mask)))

static void
draw_last_timg (void)
{
    int fmt_siz = FMT_SIZ(last_timg.fmt, last_timg.siz);

    rdram_seek(last_timg.timg);
    for (int i = 0; i < last_timg.height; i++)
    {
        for (int j = 0; j < last_timg.width; j++)
        {
            switch (fmt_siz) // TODO implement transparency in some way for all of these
            {
                case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_4b):
                    {
                        uint8_t i4_px_x2;

                        if (rdram_read(&i4_px_x2, sizeof(uint8_t), 1) != 1)
                            goto err;

                        PRINT_PX(CVT_PX(i4_px_x2, 4, 15), CVT_PX(i4_px_x2, 4, 15), CVT_PX(i4_px_x2, 4, 15));
                        j++; // TODO what happens if these 4-bit textures have an odd width?
                        PRINT_PX(CVT_PX(i4_px_x2, 0, 15), CVT_PX(i4_px_x2, 0, 15), CVT_PX(i4_px_x2, 0, 15));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_4b):
                    {
                        uint8_t ia4_px_x2;

                        if (rdram_read(&ia4_px_x2, sizeof(uint8_t), 1) != 1)
                            goto err;

                        PRINT_PX(CVT_PX(ia4_px_x2, 5, 7), CVT_PX(ia4_px_x2, 5, 7), CVT_PX(ia4_px_x2, 5, 7));
                        j++;
                        PRINT_PX(CVT_PX(ia4_px_x2, 1, 7), CVT_PX(ia4_px_x2, 1, 7), CVT_PX(ia4_px_x2, 1, 7));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_4b): // TODO previewing of CI4/CI8 textures is unreliable as the TLUT may be loaded after the index data
                    {
                        if (last_timg.tlut == 0 || last_timg.tlut_type == G_TT_NONE)
                            goto no_preview;

                        uint8_t ci8_i_x2;

                        if (rdram_read(&ci8_i_x2, sizeof(uint8_t), 1) != 1)
                            goto err;

                        uint16_t tlut_pxs[2];

                        int save_pos = rdram_pos();
                        rdram_seek(last_timg.tlut + sizeof(uint16_t) * (ci8_i_x2 >> 4));
                        if (rdram_read(&tlut_pxs[0], sizeof(uint16_t), 1) != 1)
                            goto err;
                        rdram_seek(last_timg.tlut + sizeof(uint16_t) * (ci8_i_x2 & 0xF));
                        if (rdram_read(&tlut_pxs[1], sizeof(uint16_t), 1) != 1)
                            goto err;
                        rdram_seek(save_pos);

                        for (int k = 0; k < 2; k++)
                        {
                            uint16_t tlut_px = __builtin_bswap16(tlut_pxs[k]); // TODO make more portable

                            switch (last_timg.tlut_type)
                            {
                                case G_TT_RGBA16:
                                    PRINT_PX(CVT_PX(tlut_px, 11, 31), CVT_PX(tlut_px, 6, 31), CVT_PX(tlut_px, 1, 31));
                                    break;
                                case G_TT_IA16:
                                    PRINT_PX(CVT_PX(tlut_px, 8, 255), CVT_PX(tlut_px, 8, 255), CVT_PX(tlut_px, 8, 255));
                                    break;
                            }
                        }
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_8b):
                    {
                        uint8_t i8_px;

                        if (rdram_read(&i8_px, sizeof(uint8_t), 1) != 1)
                            goto err;

                        PRINT_PX(CVT_PX(i8_px, 0, 255), CVT_PX(i8_px, 0, 255), CVT_PX(i8_px, 0, 255));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_8b):
                    {
                        uint8_t ia8_px;

                        if (rdram_read(&ia8_px, sizeof(uint8_t), 1) != 1)
                            goto err;

                        PRINT_PX(CVT_PX(ia8_px, 4, 15), CVT_PX(ia8_px, 4, 15), CVT_PX(ia8_px, 4, 15));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_8b):
                    {
                        if (last_timg.tlut == 0 || last_timg.tlut_type == G_TT_NONE)
                            goto no_preview;

                        uint8_t ci8_i;

                        if (rdram_read(&ci8_i, sizeof(uint8_t), 1) != 1)
                            goto err;

                        // if (ci8_i > last_timg.tlut_count)
                        //     goto err;

                        uint16_t tlut_px;

                        int save_pos = rdram_pos();
                        rdram_seek(last_timg.tlut + sizeof(uint16_t) * ci8_i);
                        if (rdram_read(&tlut_px, sizeof(uint16_t), 1) != 1)
                            goto err;
                        rdram_seek(save_pos);

                        tlut_px = __builtin_bswap16(tlut_px); // TODO make more portable

                        switch (last_timg.tlut_type)
                        {
                            case G_TT_RGBA16:
                                PRINT_PX(CVT_PX(tlut_px, 11, 31), CVT_PX(tlut_px, 6, 31), CVT_PX(tlut_px, 1, 31));
                                break;
                            case G_TT_IA16:
                                PRINT_PX(CVT_PX(tlut_px, 8, 255), CVT_PX(tlut_px, 8, 255), CVT_PX(tlut_px, 8, 255));
                                break;
                        }
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_16b):
                    {
                        uint16_t ia16_px;

                        if (rdram_read(&ia16_px, sizeof(uint16_t), 1) != 1)
                            goto err;

                        ia16_px = __builtin_bswap16(ia16_px); // TODO make more portable

                        // TODO most of these require the alpha channel to be visible to properly see what these look like
                        PRINT_PX(CVT_PX(ia16_px, 8, 255), CVT_PX(ia16_px, 8, 255), CVT_PX(ia16_px, 8, 255));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_16b):
                    {
                        uint16_t rgba16_px;

                        if (rdram_read(&rgba16_px, sizeof(uint16_t), 1) != 1)
                            goto err;

                        rgba16_px = __builtin_bswap16(rgba16_px); // TODO make more portable

                        PRINT_PX(CVT_PX(rgba16_px, 11, 31), CVT_PX(rgba16_px, 6, 31), CVT_PX(rgba16_px, 1, 31));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_32b):
                    {
                        uint32_t rgba32_px;

                        if (rdram_read(&rgba32_px, sizeof(uint32_t), 1) != 1)
                            goto err;

                        rgba32_px = __builtin_bswap32(rgba32_px); // TODO make more portable

                        PRINT_PX(CVT_PX(rgba32_px, 24, 255), CVT_PX(rgba32_px, 16, 255), CVT_PX(rgba32_px, 8, 255));
                    }
                    break;
                // TODO YUV? but who even uses YUV
                default:
                    goto bad_fmt_siz;
            }
        }
        printf("\x1b[0m\n");
    }
    return;

err:
    pipeline_crashed = true;
    printf("\x1b[48;2;255;0;0m" "\x1b[38;2;255;255;255m" "READ ERROR" "\x1b[0m\n");
    printf("%08lX\n", rdram_pos());
    return;

no_preview:
    printf("\x1b[48;2;255;110;0m" "\x1b[38;2;255;255;255m" "CI texture could not be previewed" "\x1b[0m\n");
    return;

bad_fmt_siz:
    pipeline_crashed = true;
    printf("\x1b[0m");
    return;
}

#define MDMASK(md)  ((((uint32_t)1 << G_MDSIZ_##md) - 1) << G_MDSFT_##md)

static void
print_othermode_hi (void)
{
    switch (othermode_hi & MDMASK(ALPHADITHER))
    {
        case G_AD_PATTERN:
            printf("G_AD_PATTERN");
            break;
        case G_AD_NOTPATTERN:
            printf("G_AD_NOTPATTERN");
            break;
        case G_AD_NOISE:
            printf("G_AD_NOISE");
            break;
        case G_AD_DISABLE:
            printf("G_AD_DISABLE");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(ALPHADITHER));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(RGBDITHER))
    {
        case G_CD_MAGICSQ:
            printf("G_CD_MAGICSQ");
            break;
        case G_CD_BAYER:
            printf("G_CD_BAYER");
            break;
        case G_CD_NOISE:
            printf("G_CD_NOISE");
            break;
        case G_CD_DISABLE:
            printf("G_CD_DISABLE");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(RGBDITHER));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(COMBKEY))
    {
        case G_CK_NONE:
            printf("G_CK_NONE");
            break;
        case G_CK_KEY:
            printf("G_CK_KEY");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(COMBKEY));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(TEXTCONV))
    {
        case G_TC_CONV:
            printf("G_TC_CONV");
            break;
        case G_TC_FILTCONV:
            printf("G_TC_FILTCONV");
            break;
        case G_TC_FILT:
            printf("G_TC_FILT");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(TEXTCONV));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(TEXTFILT))
    {
        case G_TF_POINT:
            printf("G_TF_POINT");
            break;
        case G_TF_BILERP:
            printf("G_TF_BILERP");
            break;
        case G_TF_AVERAGE:
            printf("G_TF_AVERAGE");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(TEXTFILT));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(TEXTLUT))
    {
        case G_TT_NONE:
            printf("G_TT_NONE");
            break;
        case G_TT_RGBA16:
            printf("G_TT_RGBA16");
            break;
        case G_TT_IA16:
            printf("G_TT_IA16");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(TEXTLUT));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(TEXTLOD))
    {
        case G_TL_TILE:
            printf("G_TL_TILE");
            break;
        case G_TL_LOD:
            printf("G_TL_LOD");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(TEXTLOD));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(TEXTDETAIL))
    {
        case G_TD_CLAMP:
            printf("G_TD_CLAMP");
            break;
        case G_TD_SHARPEN:
            printf("G_TD_SHARPEN");
            break;
        case G_TD_DETAIL:
            printf("G_TD_DETAIL");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(TEXTDETAIL));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(TEXTPERSP))
    {
        case G_TP_NONE:
            printf("G_TP_NONE");
            break;
        case G_TP_PERSP:
            printf("G_TP_PERSP");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(TEXTPERSP));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(CYCLETYPE))
    {
        case G_CYC_1CYCLE:
            printf("G_CYC_1CYCLE");
            break;
        case G_CYC_2CYCLE:
            printf("G_CYC_2CYCLE");
            break;
        case G_CYC_COPY:
            printf("G_CYC_COPY");
            break;
        case G_CYC_FILL:
            printf("G_CYC_FILL");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(CYCLETYPE));
            break;
    }
    printf(" | ");
    switch (othermode_hi & MDMASK(PIPELINE))
    {
        case G_PM_NPRIMITIVE:
            printf("G_PM_NPRIMITIVE");
            break;
        case G_PM_1PRIMITIVE:
            printf("G_PM_1PRIMITIVE");
            break;
        default:
            printf("0x%08X", othermode_hi & MDMASK(PIPELINE));
            break;
    }
    uint32_t unk_mask = ~(MDMASK(ALPHADITHER) | MDMASK(RGBDITHER) | MDMASK(COMBKEY) | MDMASK(TEXTCONV) | MDMASK(TEXTFILT) |
                MDMASK(TEXTLUT) | MDMASK(TEXTLOD) | MDMASK(TEXTDETAIL) | MDMASK(TEXTPERSP) |
                MDMASK(CYCLETYPE) | MDMASK(PIPELINE));
    if (othermode_hi & unk_mask)
    {
        printf(" | ");
        printf("0x%08X", othermode_hi & unk_mask);
    }
}

static int
print_rm_mode (uint32_t arg)
{
    int n = 0;

    if (arg & AA_EN)
        n += printf("AA_EN");

    if (arg & Z_CMP)
    {
        if (n > 0)
            n += printf(" | ");
        n += printf("Z_CMP");
    }

    if (arg & Z_UPD)
    {
        if (n > 0)
            n += printf(" | ");
        n += printf("Z_UPD");
    }

    if (arg & IM_RD)
    {
        if (n > 0)
            n += printf(" | ");
        n += printf("IM_RD");
    }

    if (arg & CLR_ON_CVG)
    {
        if (n > 0)
            n += printf(" | ");
        n += printf("CLR_ON_CVG");
    }

    if (n > 0)
        n += printf(" | ");

    switch (arg & 0x00000300)
    {
        case CVG_DST_CLAMP:
            n += printf("CVG_DST_CLAMP");
            break;
        case CVG_DST_WRAP:
            n += printf("CVG_DST_WRAP");
            break;
        case CVG_DST_FULL:
            n += printf("CVG_DST_FULL");
            break;
        case CVG_DST_SAVE:
            n += printf("CVG_DST_SAVE");
            break;
    }

    switch (arg & 0x00000C00)
    {
        case ZMODE_OPA:
            n += printf(" | ZMODE_OPA");
            break;
        case ZMODE_INTER:
            n += printf(" | ZMODE_INTER");
            break;
        case ZMODE_XLU:
            n += printf(" | ZMODE_XLU");
            break;
        case ZMODE_DEC:
            n += printf(" | ZMODE_DEC");
            break;
    }

    if (arg & CVG_X_ALPHA)
        n += printf(" | CVG_X_ALPHA");

    if (arg & ALPHA_CVG_SEL)
        n += printf(" | ALPHA_CVG_SEL");

    if (arg & FORCE_BL)
        n += printf(" | FORCE_BL");

    return n;
}

static int
print_rm_cbl (uint32_t arg, int c)
{
    int n = 0;
    if (c == 2)
        arg <<= 2;

    switch ((arg >> 30) & 0b11)
    {
        case G_BL_CLR_IN:
            n += printf("GBL_c%i(G_BL_CLR_IN", c);
            break;
        case G_BL_CLR_MEM:
            n += printf("GBL_c%i(G_BL_CLR_MEM", c);
            break;
        case G_BL_CLR_BL:
            n += printf("GBL_c%i(G_BL_CLR_BL", c);
            break;
        case G_BL_CLR_FOG:
            n += printf("GBL_c%i(G_BL_CLR_FOG", c);
            break;
    }
    switch ((arg >> 26) & 0b11)
    {
        case G_BL_A_IN:
            n += printf(", G_BL_A_IN");
            break;
        case G_BL_A_FOG:
            n += printf(", G_BL_A_FOG");
            break;
        case G_BL_A_SHADE:
            n += printf(", G_BL_A_SHADE");
            break;
        case G_BL_0:
            n += printf(", G_BL_0");
            break;
    }
    switch ((arg >> 22) & 0b11)
    {
        case G_BL_CLR_IN:
            n += printf(", G_BL_CLR_IN");
            break;
        case G_BL_CLR_MEM:
            n += printf(", G_BL_CLR_MEM");
            break;
        case G_BL_CLR_BL:
            n += printf(", G_BL_CLR_BL");
            break;
        case G_BL_CLR_FOG:
            n += printf(", G_BL_CLR_FOG");
            break;
    }
    switch ((arg >> 18) & 0b11)
    {
        case G_BL_1MA:
            n += printf(", G_BL_1MA)");
            break;
        case G_BL_A_MEM:
            n += printf(", G_BL_A_MEM)");
            break;
        case G_BL_1:
            n += printf(", G_BL_1)");
            break;
        case G_BL_0:
            n += printf(", G_BL_0)");
            break;
    }
    return n;
}

struct rm_preset
{
    uint32_t    rm;
    const char *name;
};

static void
print_othermode_lo (void)
{
    static const struct rm_preset rm_presets[] =
    {
        {G_RM_OPA_SURF,         "G_RM_OPA_SURF"},
        {G_RM_OPA_SURF2,        "G_RM_OPA_SURF2"},
        {G_RM_AA_OPA_SURF,      "G_RM_AA_OPA_SURF"},
        {G_RM_AA_OPA_SURF2,     "G_RM_AA_OPA_SURF2"},
        {G_RM_RA_OPA_SURF,      "G_RM_RA_OPA_SURF"},
        {G_RM_RA_OPA_SURF2,     "G_RM_RA_OPA_SURF2"},
        {G_RM_ZB_OPA_SURF,      "G_RM_ZB_OPA_SURF"},
        {G_RM_ZB_OPA_SURF2,     "G_RM_ZB_OPA_SURF2"},
        {G_RM_AA_ZB_OPA_SURF,   "G_RM_AA_ZB_OPA_SURF"},
        {G_RM_AA_ZB_OPA_SURF2,  "G_RM_AA_ZB_OPA_SURF2"},
        {G_RM_RA_ZB_OPA_SURF,   "G_RM_RA_ZB_OPA_SURF"},
        {G_RM_RA_ZB_OPA_SURF2,  "G_RM_RA_ZB_OPA_SURF2"},
        {G_RM_XLU_SURF,         "G_RM_XLU_SURF"},
        {G_RM_XLU_SURF2,        "G_RM_XLU_SURF2"},
        {G_RM_AA_XLU_SURF,      "G_RM_AA_XLU_SURF"},
        {G_RM_AA_XLU_SURF2,     "G_RM_AA_XLU_SURF2"},
        {G_RM_ZB_XLU_SURF,      "G_RM_ZB_XLU_SURF"},
        {G_RM_ZB_XLU_SURF2,     "G_RM_ZB_XLU_SURF2"},
        {G_RM_AA_ZB_XLU_SURF,   "G_RM_AA_ZB_XLU_SURF"},
        {G_RM_AA_ZB_XLU_SURF2,  "G_RM_AA_ZB_XLU_SURF2"},
        {G_RM_ZB_OPA_DECAL,     "G_RM_ZB_OPA_DECAL"},
        {G_RM_ZB_OPA_DECAL2,    "G_RM_ZB_OPA_DECAL2"},
        {G_RM_AA_ZB_OPA_DECAL,  "G_RM_AA_ZB_OPA_DECAL"},
        {G_RM_AA_ZB_OPA_DECAL2, "G_RM_AA_ZB_OPA_DECAL2"},
        {G_RM_RA_ZB_OPA_DECAL,  "G_RM_RA_ZB_OPA_DECAL"},
        {G_RM_RA_ZB_OPA_DECAL2, "G_RM_RA_ZB_OPA_DECAL2"},
        {G_RM_ZB_XLU_DECAL,     "G_RM_ZB_XLU_DECAL"},
        {G_RM_ZB_XLU_DECAL2,    "G_RM_ZB_XLU_DECAL2"},
        {G_RM_AA_ZB_XLU_DECAL,  "G_RM_AA_ZB_XLU_DECAL"},
        {G_RM_AA_ZB_XLU_DECAL2, "G_RM_AA_ZB_XLU_DECAL2"},
        {G_RM_AA_ZB_OPA_INTER,  "G_RM_AA_ZB_OPA_INTER"},
        {G_RM_AA_ZB_OPA_INTER2, "G_RM_AA_ZB_OPA_INTER2"},
        {G_RM_RA_ZB_OPA_INTER,  "G_RM_RA_ZB_OPA_INTER"},
        {G_RM_RA_ZB_OPA_INTER2, "G_RM_RA_ZB_OPA_INTER2"},
        {G_RM_AA_ZB_XLU_INTER,  "G_RM_AA_ZB_XLU_INTER"},
        {G_RM_AA_ZB_XLU_INTER2, "G_RM_AA_ZB_XLU_INTER2"},
        {G_RM_AA_XLU_LINE,      "G_RM_AA_XLU_LINE"},
        {G_RM_AA_XLU_LINE2,     "G_RM_AA_XLU_LINE2"},
        {G_RM_AA_ZB_XLU_LINE,   "G_RM_AA_ZB_XLU_LINE"},
        {G_RM_AA_ZB_XLU_LINE2,  "G_RM_AA_ZB_XLU_LINE2"},
        {G_RM_AA_DEC_LINE,      "G_RM_AA_DEC_LINE"},
        {G_RM_AA_DEC_LINE2,     "G_RM_AA_DEC_LINE2"},
        {G_RM_AA_ZB_DEC_LINE,   "G_RM_AA_ZB_DEC_LINE"},
        {G_RM_AA_ZB_DEC_LINE2,  "G_RM_AA_ZB_DEC_LINE2"},
        {G_RM_TEX_EDGE,         "G_RM_TEX_EDGE"},
        {G_RM_TEX_EDGE2,        "G_RM_TEX_EDGE2"},
        {G_RM_AA_TEX_EDGE,      "G_RM_AA_TEX_EDGE"},
        {G_RM_AA_TEX_EDGE2,     "G_RM_AA_TEX_EDGE2"},
        {G_RM_AA_ZB_TEX_EDGE,   "G_RM_AA_ZB_TEX_EDGE"},
        {G_RM_AA_ZB_TEX_EDGE2,  "G_RM_AA_ZB_TEX_EDGE2"},
        {G_RM_AA_ZB_TEX_INTER,  "G_RM_AA_ZB_TEX_INTER"},
        {G_RM_AA_ZB_TEX_INTER2, "G_RM_AA_ZB_TEX_INTER2"},
        {G_RM_AA_SUB_SURF,      "G_RM_AA_SUB_SURF"},
        {G_RM_AA_SUB_SURF2,     "G_RM_AA_SUB_SURF2"},
        {G_RM_AA_ZB_SUB_SURF,   "G_RM_AA_ZB_SUB_SURF"},
        {G_RM_AA_ZB_SUB_SURF2,  "G_RM_AA_ZB_SUB_SURF2"},
        {G_RM_PCL_SURF,         "G_RM_PCL_SURF"},
        {G_RM_PCL_SURF2,        "G_RM_PCL_SURF2"},
        {G_RM_AA_PCL_SURF,      "G_RM_AA_PCL_SURF"},
        {G_RM_AA_PCL_SURF2,     "G_RM_AA_PCL_SURF2"},
        {G_RM_ZB_PCL_SURF,      "G_RM_ZB_PCL_SURF"},
        {G_RM_ZB_PCL_SURF2,     "G_RM_ZB_PCL_SURF2"},
        {G_RM_AA_ZB_PCL_SURF,   "G_RM_AA_ZB_PCL_SURF"},
        {G_RM_AA_ZB_PCL_SURF2,  "G_RM_AA_ZB_PCL_SURF2"},
        {G_RM_AA_OPA_TERR,      "G_RM_AA_OPA_TERR"},
        {G_RM_AA_OPA_TERR2,     "G_RM_AA_OPA_TERR2"},
        {G_RM_AA_ZB_OPA_TERR,   "G_RM_AA_ZB_OPA_TERR"},
        {G_RM_AA_ZB_OPA_TERR2,  "G_RM_AA_ZB_OPA_TERR2"},
        {G_RM_AA_TEX_TERR,      "G_RM_AA_TEX_TERR"},
        {G_RM_AA_TEX_TERR2,     "G_RM_AA_TEX_TERR2"},
        {G_RM_AA_ZB_TEX_TERR,   "G_RM_AA_ZB_TEX_TERR"},
        {G_RM_AA_ZB_TEX_TERR2,  "G_RM_AA_ZB_TEX_TERR2"},
        {G_RM_AA_SUB_TERR,      "G_RM_AA_SUB_TERR"},
        {G_RM_AA_SUB_TERR2,     "G_RM_AA_SUB_TERR2"},
        {G_RM_AA_ZB_SUB_TERR,   "G_RM_AA_ZB_SUB_TERR"},
        {G_RM_AA_ZB_SUB_TERR2,  "G_RM_AA_ZB_SUB_TERR2"},
        {G_RM_CLD_SURF,         "G_RM_CLD_SURF"},
        {G_RM_CLD_SURF2,        "G_RM_CLD_SURF2"},
        {G_RM_ZB_CLD_SURF,      "G_RM_ZB_CLD_SURF"},
        {G_RM_ZB_CLD_SURF2,     "G_RM_ZB_CLD_SURF2"},
        {G_RM_ZB_OVL_SURF,      "G_RM_ZB_OVL_SURF"},
        {G_RM_ZB_OVL_SURF2,     "G_RM_ZB_OVL_SURF2"},
        {G_RM_ADD,              "G_RM_ADD"},
        {G_RM_ADD2,             "G_RM_ADD2"},
        {G_RM_VISCVG,           "G_RM_VISCVG"},
        {G_RM_VISCVG2,          "G_RM_VISCVG2"},
        {G_RM_OPA_CI,           "G_RM_OPA_CI"},
        {G_RM_OPA_CI2,          "G_RM_OPA_CI2"},
        {G_RM_RA_SPRITE,        "G_RM_RA_SPRITE"},
        {G_RM_RA_SPRITE2,       "G_RM_RA_SPRITE2"},
    };
    static const struct rm_preset bl1_presets[] =
    {
        {G_RM_FOG_SHADE_A,      "G_RM_FOG_SHADE_A"},
        {G_RM_FOG_PRIM_A,       "G_RM_FOG_PRIM_A"},
        {G_RM_PASS,             "G_RM_PASS"},
        {G_RM_NOOP,             "G_RM_NOOP"},
    };
    static const struct rm_preset bl2_presets[] =
    {
        {G_RM_NOOP2,            "G_RM_NOOP2"},
    };
#define MDMASK_RM_C1	((uint32_t)0xCCCC0000)
#define MDMASK_RM_C2	((uint32_t)0x33330000)
#define MDMASK_RM_LO	((uint32_t)0x0000FFF8)
#define RM_MASK         (MDMASK_RM_C1 | MDMASK_RM_C2 | MDMASK_RM_LO)

    const struct rm_preset *pre_c1 = NULL;
    const struct rm_preset *pre_c2 = NULL;
    int n = 0;

    for (int i = 0; i < ARRAY_COUNT(rm_presets); i++)
    {
        const struct rm_preset *pre = &rm_presets[i];

        uint32_t rm_c1 = othermode_lo & (MDMASK_RM_C1 | MDMASK_RM_LO | (pre->rm & ~RM_MASK));
        if (!pre_c1 && rm_c1 == pre->rm)
            pre_c1 = pre;

        uint32_t rm_c2 = othermode_lo & (MDMASK_RM_C2 | MDMASK_RM_LO | (pre->rm & ~RM_MASK));
        if (!pre_c2 && rm_c2 == pre->rm)
            pre_c2 = pre;
    }

    if (!pre_c1 || !pre_c2 || pre_c1 + 1 != pre_c2)
    {
        for (int i = 0; i < ARRAY_COUNT(bl1_presets); i++)
        {
            const struct rm_preset *pre = &bl1_presets[i];

            uint32_t rm_c1 = othermode_lo & (MDMASK_RM_C1 | (pre->rm & ~RM_MASK));
            if (rm_c1 == pre->rm)
            {
                pre_c1 = pre;
                break;
            }
        }

        for (int i = 0; i < ARRAY_COUNT(bl2_presets); i++)
        {
            const struct rm_preset *pre = &bl2_presets[i];

            uint32_t rm_c2 = othermode_lo & (MDMASK_RM_C2 | (pre->rm & ~RM_MASK));
            if (rm_c2 == pre->rm)
            {
                pre_c2 = pre;
                break;
            }
        }
    }

    uint32_t pre_rm = 0;

    if (pre_c1)
        pre_rm |= pre_c1->rm;

    if (pre_c2)
        pre_rm |= pre_c2->rm;

    switch (othermode_lo & MDMASK(ALPHACOMPARE))
    {
        case G_AC_NONE:
            printf("G_AC_NONE");
            break;
        case G_AC_THRESHOLD:
            printf("G_AC_THRESHOLD");
            break;
        case G_AC_DITHER:
            printf("G_AC_DITHER");
            break;
        default:
            printf("0x%08X", othermode_lo & MDMASK(ALPHACOMPARE));
            break;
    }

    printf(" | ");

    switch (othermode_lo & MDMASK(ZSRCSEL))
    {
        case G_ZS_PIXEL:
            printf("G_ZS_PIXEL");
            break;
        case G_ZS_PRIM:
            printf("G_ZS_PRIM");
            break;
        default:
            printf("0x%08X", othermode_lo & MDMASK(ZSRCSEL));
            break;
    }

    uint32_t rm = othermode_lo & (RM_MASK | pre_rm);

    if ((othermode_lo & ~pre_rm) & MDMASK_RM_LO)
    {
        printf(" | ");
        print_rm_mode(rm);
    }

    printf(" | ");
    if (pre_c1)
        printf("%s", pre_c1->name);
    else
        print_rm_cbl(rm, 1);

    printf(" | ");
    if (pre_c2)
        printf("%s", pre_c2->name);
    else
        print_rm_cbl(rm, 2);

    uint32_t unk_mask = ~(RM_MASK | MDMASK(ALPHACOMPARE) | MDMASK(ZSRCSEL));
    if (othermode_lo & unk_mask)
    {
        printf(" | ");
        printf("0x%08X", othermode_lo & unk_mask);
    }
}

static void
print_othermode (void)
{
    print_othermode_hi();
    printf(", ");
    print_othermode_lo();
}

static void 
print_geometrymode (void)
{
    struct
    {
        uint32_t value;
        const char *name;
    } geometry_modes[] =
    {
        F3DZEX_CONST(G_ZBUFFER),             F3DZEX_CONST(G_TEXTURE_ENABLE),
        F3DZEX_CONST(G_SHADE),               F3DZEX_CONST(G_SHADING_SMOOTH),
        F3DZEX_CONST(G_CULL_FRONT),          F3DZEX_CONST(G_CULL_BACK),
        F3DZEX_CONST(G_FOG),                 F3DZEX_CONST(G_LIGHTING),
        F3DZEX_CONST(G_TEXTURE_GEN),         F3DZEX_CONST(G_TEXTURE_GEN_LINEAR),
        F3DZEX_CONST(G_LOD),                 F3DZEX_CONST(G_CLIPPING),
        F3DZEX_CONST(G_LIGHTING_POSITIONAL),
    };
    bool first = true;
    uint32_t gm = geometry_mode;

    for (int i = 0; i < ARRAY_COUNT(geometry_modes); i++)
    {
        if ((geometry_modes[i].value & gm) == 0)
            continue;

        if (!first)
            printf(" | ");

        printf("%s", geometry_modes[i].name);
        gm &= ~geometry_modes[i].value;
        first = false;
    }
    if (gm != 0)
    {
        if (!first)
            printf(" | ");
        printf("0x%08X", gm);
    }
    else if (first)
        printf("0");
}

static const char *
cc_str (int v, int i)
{
    switch (i)
    {
        case 0: /* CC 1 A */
        case 8: /* CC 2 A */
            {
                static const char *cc1a[] =
                    { 
                        "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", 
                        "SHADE", "ENVIRONMENT", "1", "NOISE",
                    };
                if (v >= ARRAY_COUNT(cc1a))
                    return "0";
                return cc1a[v];
            }
            break;
        case 1: /* CC 1 B */
        case 9: /* CC 2 B */
            {
                static const char *cc1b[] = 
                    { 
                        "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", 
                        "SHADE", "ENVIRONMENT", "CENTER", "K4",
                    };
                if (v >= ARRAY_COUNT(cc1b))
                    return "0";
                return cc1b[v];
            }
            break;
        case  2: /* CC 1 C */
        case 10: /* CC 2 C */
            {
                static const char *cc1c[] =
                    { 
                        "COMBINED", "TEXEL0", "TEXEL1", "PRIMiTIVE", 
                        "SHADE", "ENVIRONMENT", "CENTER", "COMBINED_ALPHA", 
                        "TEXEL0_ALPHA", "TEXEL1_ALPHA", "PRIMITIVE_ALPHA", "SHADE_ALPHA", 
                        "ENV_ALPHA", "LOD_FRACTION", "PRIM_LOD_FRAC", "K5",
                    };
                if (v >= ARRAY_COUNT(cc1c))
                    return "0";
                return cc1c[v];
            }
            break;
        case  3: /* CC 1 D */
        case 11: /* CC 2 D */
            {
                static const char *cc1d[] =
                    { 
                        "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", 
                        "SHADE", "ENVIRONMENT", "1", 
                    };
                if (v >= ARRAY_COUNT(cc1d))
                    return "0";
                return cc1d[v];
            }
            break;
        case 4:  /* AC 1 A */
        case 12: /* AC 2 A */
        case 5:  /* AC 1 B */
        case 13: /* AC 2 B */
        case 7:  /* AC 1 D */
        case 15: /* AC 2 D */
            {
                static const char *ac1abd[] =
                    { 
                        "COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE",
                        "SHADE", "ENVIRONMENT", "1",
                    };
                if (v >= ARRAY_COUNT(ac1abd))
                    return "0";
                return ac1abd[v];
            }
            break;
        case 6:  /* AC 1 C */
        case 14: /* AC 2 C */
            {
                static const char *ac1c[] =
                    { 
                        "LOD_FRACTION", "TEXEL0", "TEXEL1", "PRIMITIVE", 
                        "SHADE", "ENVIRONMENT", "PRIM_LOD_FRAC",
                    };
                if (v >= ARRAY_COUNT(ac1c))
                    return "0";
                return ac1c[v];
            }
            break;
    }
    return "?";
}

static void
print_cc (void)
{
    printf("%s, %s, %s, %s" ", " "%s, %s, %s, %s" ", " "%s, %s, %s, %s" ", " "%s, %s, %s, %s", 
            cc_str(_SHIFTR(combine_mode_hi, 4, 20), 0), 
            cc_str(_SHIFTR(combine_mode_lo, 4, 28), 1), 
            cc_str(_SHIFTR(combine_mode_hi, 5, 15), 2), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 15), 3), 
            cc_str(_SHIFTR(combine_mode_hi, 3, 12), 4), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 12), 5), 
            cc_str(_SHIFTR(combine_mode_hi, 3, 9),  6), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 9),  7), 
            cc_str(_SHIFTR(combine_mode_hi, 4, 5),  8), 
            cc_str(_SHIFTR(combine_mode_lo, 4, 24), 9), 
            cc_str(_SHIFTR(combine_mode_hi, 5, 0),  10), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 6),  11), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 21), 12), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 3),  13), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 18), 14), 
            cc_str(_SHIFTR(combine_mode_lo, 3, 0),  15));
}

static void
print_mtx_params (void)
{
    struct
    {
        uint32_t value;
        const char *set_name;
        const char *unset_name;
    } mtx_params[] =
    {
        F3DZEX_MTX_PARAM(G_MTX_PUSH, G_MTX_NOPUSH),
        F3DZEX_MTX_PARAM(G_MTX_LOAD, G_MTX_MUL),
        F3DZEX_MTX_PARAM(G_MTX_PROJECTION, G_MTX_MODELVIEW),
    };
    bool first = true;
    uint32_t params = last_mtx.params;

    for (int i = 0; i < ARRAY_COUNT(mtx_params); i++)
    {
        if (!first)
            printf(" | ");

        if (mtx_params[i].value & params)
            printf("%s", mtx_params[i].set_name);
        else
            printf("%s", mtx_params[i].unset_name);
        params &= ~mtx_params[i].value;
        first = false;
    }
    if (params != 0)
    {
        if (!first)
            printf(" | ");
        printf("0x%08X", params);
    }
}

static void
print_segments (void)
{
    for (int i = 0; i < 16; i++)
    {
        printf("%02X : %08X", i, segment_table[i]);
        if ((i + 1) % 4 == 0)
            printf("\n");
        else
            printf(" | ");
    }
}

static void
print_last_vtx (void)
{
    rdram_seek(last_vtx.vtx);

    for (int i = 0; i < last_vtx.num; i++)
    {
        Vtx vtx;

        if (rdram_read(&vtx, sizeof(Vtx), 1) != 1)
            goto err;

        vtx.v.ob[0] = __builtin_bswap16(vtx.v.ob[0]);
        vtx.v.ob[1] = __builtin_bswap16(vtx.v.ob[1]);
        vtx.v.ob[2] = __builtin_bswap16(vtx.v.ob[2]);
        vtx.v.flag  = __builtin_bswap16(vtx.v.flag);
        vtx.v.tc[0] = __builtin_bswap16(vtx.v.tc[0]);
        vtx.v.tc[1] = __builtin_bswap16(vtx.v.tc[1]);

        printf("\t{ { { %6d, %6d, %6d }, %d, { %6d, %6d }, { %4d, %4d, %4d, %4d } } }\n", 
                vtx.v.ob[0], vtx.v.ob[1], vtx.v.ob[2], 
                vtx.v.flag, 
                vtx.v.tc[0], vtx.v.tc[1], 
                vtx.v.cn[0], vtx.v.cn[1], vtx.v.cn[2], vtx.v.cn[3]);
    }
    return;
err:
    pipeline_crashed = true;
    printf("\x1b[48;2;255;0;0m" "\x1b[38;2;255;255;255m" "READ ERROR" "\x1b[0m\n");
    return;
}

static void
print_last_mtx (void)
{
    MtxF mf;
    Mtx mtx;

    if (rdram_seek(last_mtx.mtx) || rdram_read(&mtx, sizeof(Mtx), 1) != 1)
        goto err;

    mtx_to_mtxf(&mf, &mtx, true);

    printf("\t"  "/ %13.6f  %13.6f  %13.6f  %13.6f \\" "\n"
           "\t"  "| %13.6f  %13.6f  %13.6f  %13.6f |"  "\n"
           "\t"  "| %13.6f  %13.6f  %13.6f  %13.6f |"  "\n"
           "\t" "\\ %13.6f  %13.6f  %13.6f  %13.6f /"  "\n",
        mf.mf[0][0], mf.mf[0][1], mf.mf[0][2], mf.mf[0][3],
        mf.mf[1][0], mf.mf[1][1], mf.mf[1][2], mf.mf[1][3],
        mf.mf[2][0], mf.mf[2][1], mf.mf[2][2], mf.mf[2][3],
        mf.mf[3][0], mf.mf[3][1], mf.mf[3][2], mf.mf[3][3]);

    printf("PARAMS: ");
    print_mtx_params();
    printf("\n");

    return;
err:
    pipeline_crashed = true;
    printf("\x1b[48;2;255;0;0m" "\x1b[38;2;255;255;255m" "READ ERROR" "\x1b[0m\n");
    return;
}

static void
print_string_at_addr (uint32_t addr, print_fn pfn)
{
    iconv_t cd;
    char c;
    char *in_buf, *out_buf, *iconv_in_buf, *iconv_out_buf;
    size_t in_bytes_left, out_bytes_left;
    size_t str_len = 0;

    /* convert address */
    addr = segmented_to_physical(addr);

    /* determine the length of the string */
    rdram_seek(addr);
    do {
        if (rdram_read(&c, sizeof(c), 1) != 1)
            return;
        str_len++;
    } while (c != '\0');

    /* open string */
    pfn("\"");

    /* read the whole string */
    in_buf = malloc(sizeof(char) * str_len);
    out_buf = malloc(sizeof(wchar_t) * str_len);

    rdram_seek(addr);
    if (rdram_read(in_buf, sizeof(char), str_len) != str_len)
        goto err;

    /* convert string from EUC-JP to UTF-8 */
    cd = iconv_open("UTF-8", "EUC-JP");

    in_bytes_left = str_len;
    iconv_in_buf = in_buf;
    iconv_out_buf = out_buf;
    iconv(cd, &iconv_in_buf, &in_bytes_left, &iconv_out_buf, &out_bytes_left);
    iconv_close(cd);

    /* print converted string */
    pfn("%s", out_buf);

err:
    /* close string */
    pfn("\"");

    free(in_buf);
    free(out_buf);
}

static void
decode_noop_cmd (void)
{
    const gfxd_value_t *noop_type = gfxd_arg_value(0 /* type */);
    const gfxd_value_t *noop_data = gfxd_arg_value(1 /* data */);
    const gfxd_value_t *noop_data1 = gfxd_arg_value(2 /* data1 */);

    switch (noop_type->u)
    {
        // TODO these break dynamic macros
        case 1:
            gfxd_printf("gsDPNoOpHere(");
            print_string_at_addr(noop_data->u, gfxd_printf);
            gfxd_printf(", 0x%04X),", noop_data->u, noop_data1->u);
            break;
        case 2:
            gfxd_printf("gsDPNoOpString(");
            print_string_at_addr(noop_data->u, gfxd_printf);
            gfxd_printf(", 0x%04X),",  noop_data1->u);
            break;
        case 3:
            gfxd_printf("gsDPNoOpWord(0x%08X, 0x%04X),", noop_data->u, noop_data1->u);
            break;
        case 4:
            gfxd_printf("gsDPNoOpFloat(0x%08X, 0x%04X),", noop_data->u, noop_data1->u);
            break;
        case 5:
            if (noop_data->u != 0)
                goto emit_noop_tag3;

            if (noop_data1->u == 0)
                gfxd_printf("gsDPNoOpQuiet(),");
            else
                gfxd_printf("gsDPNoOpVerbose(0x%04X),", noop_data1->u);
            break;
        case 6:
            gfxd_printf("gsDPNoOpCallBack(0x%08X, 0x%04X),", noop_data->u, noop_data1->u);
            break;
        case 7:
            gfxd_printf("gsDPNoOpOpenDisp(");
            print_string_at_addr(noop_data->u, gfxd_printf);
            gfxd_printf(", %d),",  noop_data1->u);

            last_open_disps.file = noop_data->u;
            last_open_disps.line = noop_data1->u;
            break;
        case 8:
            gfxd_printf("gsDPNoOpCloseDisp(");
            print_string_at_addr(noop_data->u, gfxd_printf);
            gfxd_printf(", %d),",  noop_data1->u);
            break;
        default:
emit_noop_tag3:
            // TODO add warning for unknown NoOpTag variant
            gfxd_printf("%s(0x%02X, 0x%08X, 0x%04X),", gfxd_macro_name(), noop_type->u, noop_data->u, noop_data1->u);
            break;
    }
}

static int
macro_fn (void)
{
    int m_id = gfxd_macro_id();
    const gfxd_value_t *arg_value;
    int tlut_value;
    uint32_t om_hi = 0;
    uint32_t om_lo = 0;
    uint32_t gm_clr = 0;
    uint32_t gm_set = 0;
    uint32_t cc_hi = 0;
    uint32_t cc_lo = 0;

    // for (int i = 0; i < dl_stack_top + 2; i++)
    gfxd_printf("  /* %6d %08X */  ", n_gfx, gfx_addr);

    if (m_id == gfxd_DPNoOpTag3)
    {
        /* decode special NoOps */
        decode_noop_cmd();
    }
    else
    {
        /* Execute the default macro handler */
        gfxd_macro_dflt();
        gfxd_puts(",");
    }

    last_macro_size = gfxd_macro_packets() * sizeof(Gfx);

    switch (m_id)
    {
        case gfxd_SPEndDisplayList:
            if (dl_stack_top == -1) // dl stack empty, task is done
                task_done = true;
            else
                gfx_addr = dl_stack_pop();
            // printf("[DisplayList Stack] POP 0x%08lx\n", gfx_addr);
            break;
        case gfxd_SPMatrix:
            arg_value = gfxd_value_by_type(gfxd_Mtxptr, 0);
            if (arg_value != NULL)
                last_mtx.mtx = segmented_to_physical(arg_value->u);
            arg_value = gfxd_value_by_type(gfxd_Mtxparam, 0);
            if (arg_value != NULL)
                last_mtx.params = arg_value->i;
            break;
        case gfxd_DPSetCombineLERP:
            arg_value = gfxd_value_by_type(gfxd_Ccmuxa, 0);
            if (arg_value != NULL)
                cc_hi |= _SHIFTL(arg_value->i, 4, 20);
            arg_value = gfxd_value_by_type(gfxd_Ccmuxb, 0);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 4, 28);
            arg_value = gfxd_value_by_type(gfxd_Ccmuxc, 0);
            if (arg_value != NULL)
                cc_hi |= _SHIFTL(arg_value->i, 5, 15);
            arg_value = gfxd_value_by_type(gfxd_Ccmuxd, 0);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 15);
            arg_value = gfxd_value_by_type(gfxd_Acmuxabd, 0);
            if (arg_value != NULL)
                cc_hi |= _SHIFTL(arg_value->i, 3, 12);
            arg_value = gfxd_value_by_type(gfxd_Acmuxabd, 1);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 12);
            arg_value = gfxd_value_by_type(gfxd_Acmuxc, 0);
            if (arg_value != NULL)
                cc_hi |= _SHIFTL(arg_value->i, 3, 9);
            arg_value = gfxd_value_by_type(gfxd_Acmuxabd, 2);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 9);
            arg_value = gfxd_value_by_type(gfxd_Ccmuxa, 1);
            if (arg_value != NULL)
                cc_hi |= _SHIFTL(arg_value->i, 4, 5);
            arg_value = gfxd_value_by_type(gfxd_Ccmuxb, 1);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 4, 24);
            arg_value = gfxd_value_by_type(gfxd_Ccmuxc, 1);
            if (arg_value != NULL)
                cc_hi |= _SHIFTL(arg_value->i, 5, 0);
            arg_value = gfxd_value_by_type(gfxd_Ccmuxd, 1);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 6);
            arg_value = gfxd_value_by_type(gfxd_Acmuxabd, 3);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 21);
            arg_value = gfxd_value_by_type(gfxd_Acmuxabd, 4);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 3);
            arg_value = gfxd_value_by_type(gfxd_Acmuxc, 1);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 18);
            arg_value = gfxd_value_by_type(gfxd_Acmuxabd, 5);
            if (arg_value != NULL)
                cc_lo |= _SHIFTL(arg_value->i, 3, 0);
            goto cc_set;
        case gfxd_DPSetCombineMode:
            arg_value = gfxd_value_by_type(gfxd_Ccpre, 0);
            if (arg_value != NULL)
                cc_hi = arg_value->u;
            arg_value = gfxd_value_by_type(gfxd_Ccpre, 1);
            if (arg_value != NULL)
                cc_lo = arg_value->u;
cc_set:
            combine_mode_hi = cc_hi;
            combine_mode_lo = cc_lo;
            break;
        case gfxd_SPGeometryMode:
            arg_value = gfxd_value_by_type(gfxd_Gm, 0);
            if (arg_value != NULL)
                gm_clr = arg_value->u;
            arg_value = gfxd_value_by_type(gfxd_Gm, 1);
            if (arg_value != NULL)
                gm_set = arg_value->u;
            goto geometrymode_set;
        case gfxd_SPLoadGeometryMode:
            gm_clr = 0xFFFFFFFF;
        case gfxd_SPSetGeometryMode:
            arg_value = gfxd_value_by_type(gfxd_Gm, 0);
            if (arg_value != NULL)
                gm_set = arg_value->u;
            goto geometrymode_set;
        case gfxd_SPClearGeometryMode:
            arg_value = gfxd_value_by_type(gfxd_Gm, 0);
            if (arg_value != NULL)
                gm_clr = arg_value->u;
geometrymode_set:
            geometry_mode &= ~gm_clr;
            geometry_mode |= gm_set;
            break;
        case gfxd_DPPipelineMode:
            arg_value = gfxd_value_by_type(gfxd_Pm, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(PIPELINE);
            goto othermode_set;
        case gfxd_DPSetAlphaDither:
            arg_value = gfxd_value_by_type(gfxd_Ad, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(ALPHADITHER);
            goto othermode_set;
        case gfxd_DPSetColorDither:
            arg_value = gfxd_value_by_type(gfxd_Cd, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(RGBDITHER);
            goto othermode_set;
        case gfxd_DPSetTextureConvert:
            arg_value = gfxd_value_by_type(gfxd_Tc, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(TEXTCONV);
            goto othermode_set;
        case gfxd_DPSetCycleType:
            arg_value = gfxd_value_by_type(gfxd_Cyc, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(CYCLETYPE);
            goto othermode_set;
        case gfxd_DPSetCombineKey:
            arg_value = gfxd_value_by_type(gfxd_Ck, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(COMBKEY);
            goto othermode_set;
        case gfxd_DPSetTextureDetail:
            arg_value = gfxd_value_by_type(gfxd_Td, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(TEXTDETAIL);
            goto othermode_set;
        case gfxd_DPSetTextureFilter:
            arg_value = gfxd_value_by_type(gfxd_Tf, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(TEXTFILT);
            goto othermode_set;
        case gfxd_DPSetTextureLOD:
            arg_value = gfxd_value_by_type(gfxd_Tl, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(TEXTLOD);
            goto othermode_set;
        case gfxd_DPSetTextureLUT:
            arg_value = gfxd_value_by_type(gfxd_Tt, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(TEXTLUT);
            goto othermode_set;
        case gfxd_DPSetTexturePersp:
            arg_value = gfxd_value_by_type(gfxd_Tp, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u & MDMASK(TEXTPERSP);
            goto othermode_set;
        case gfxd_DPSetAlphaCompare:
            arg_value = gfxd_value_by_type(gfxd_Ac, 0);
            if (arg_value != NULL)
                om_lo |= arg_value->u & MDMASK(ALPHACOMPARE);
            goto othermode_set;
        case gfxd_DPSetDepthSource:
            arg_value = gfxd_value_by_type(gfxd_Zs, 0);
            if (arg_value != NULL)
                om_lo |= arg_value->u & MDMASK(ZSRCSEL);
            goto othermode_set;
        case gfxd_DPSetRenderMode:
            arg_value = gfxd_value_by_type(gfxd_Rm1, 0);
            if (arg_value != NULL)
                om_lo |= arg_value->u & MDMASK(RENDERMODE);
            arg_value = gfxd_value_by_type(gfxd_Rm2, 0);
            if (arg_value != NULL)
                om_lo |= arg_value->u & MDMASK(RENDERMODE);
            goto othermode_set;
        case gfxd_SPSetOtherMode:
        case gfxd_DPSetOtherMode:
        case gfxd_SPSetOtherModeLo:
        case gfxd_SPSetOtherModeHi:
            arg_value = gfxd_value_by_type(gfxd_Othermodehi, 0);
            if (arg_value != NULL)
                om_hi |= arg_value->u;
            arg_value = gfxd_value_by_type(gfxd_Othermodelo, 0);
            if (arg_value != NULL)
                om_lo |= arg_value->u;
othermode_set:
            // printf("\nFOUND OTHERMODE MUTATION: HI %08X LO %08X\n", om_hi, om_lo);
            othermode_hi = om_hi;
            othermode_lo = om_lo;

            last_tlut.type = om_hi & MDMASK(TEXTLUT);
            break;
        case gfxd_Invalid:
            // TODO not all invalid commands crash console, but some might?
            // TODO at least add a warning that there were invalid commands somewhere in the buffer
            // pipeline_crashed = true;
            break;
        case gfxd_DPFullSync:
            // task_done = true;
            break;
    }

    gfxd_puts("\n");
    return 1; /* Non-zero to step one (possibly compound) macro at a time */
}

/**************************************************************************
 *  IO Callbacks
 */

static int
empty_output_callback (const char *buf, int count)
{
    /* Drop the printed string */
    return count;
}

static int
input_callback (void *buf, int count)
{
    rdram_seek(gfx_addr);
    return rdram_read(buf, 1, count);
}

/**************************************************************************
 *  
 */

/**
 *  Ran when a G_DL command is encountered.
 *  For gsSPDisplayList, it saves the "return address" on to the displaylist stack
 *   and jumps to the destination.
 *  For gsSPBranchList, it jumps to the destination without pushing a return address
 *   to the displaylist stack.
 */
static int
dl_handler (uint32_t dl)
{
    if (gfxd_macro_id() == gfxd_SPDisplayList)
    {
        // printf("[DisplayList Stack] PUSH 0x%08x\n", dl);
        dl_stack_push(gfx_addr);
    }
    gfx_addr = segmented_to_physical(dl) - sizeof(Gfx);
    return 0;
}

static int 
seg_handler (uint32_t seg, int32_t num)
{
    segment_table[num] = seg & ~KSEG_MASK;
    return 0;
}

/**
 *  Ran when a LoadUcode command is encountered.
 *  Swaps the ucode to disassemble if it is recognized.
 */
static int
uctext_handler (uint32_t text, uint32_t size)
{
    // TODO currently using hardcoded addresses for debug, move to cfg file?
    text &= ~KSEG_MASK;

    if (text == (0x80113070 & ~KSEG_MASK)) // S2DEX2 text start
        next_ucode = gfxd_s2dex2;
    else if (text == (0x80155F50 & ~KSEG_MASK)) // F3DEX2 text start
        next_ucode = gfxd_f3dex2;
    else
        pipeline_crashed = true; // unknown ucode

    return 0;
}

static int
cimg_handler (uint32_t cimg, int32_t fmt, int32_t siz, int32_t width)
{
    if (!((fmt == G_IM_FMT_CI   && (siz == G_IM_SIZ_8b)) || 
          (fmt == G_IM_FMT_I    && (siz == G_IM_SIZ_4b || siz == G_IM_SIZ_8b)) || 
          (fmt == G_IM_FMT_IA   && (siz == G_IM_SIZ_8b || siz == G_IM_SIZ_16b)) || 
          (fmt == G_IM_FMT_RGBA && (siz == G_IM_SIZ_16b || siz == G_IM_SIZ_32b))))
        pipeline_crashed = true;
    return 0;
}

static int
timg_handler (uint32_t timg, int32_t fmt, int32_t siz, int32_t width, int32_t height, int32_t pal)
{
    last_timg.timg = segmented_to_physical(timg);
    last_timg.fmt = fmt;
    last_timg.siz = siz;
    last_timg.width = width;
    last_timg.height = height;
    last_timg.pal = pal;

    if (fmt == G_IM_FMT_CI)
    {
        if (last_tlut.tlut != 0)
        {
            last_timg.tlut = last_tlut.tlut;
            last_timg.tlut_count = last_tlut.count;
            last_timg.tlut_idx = last_tlut.idx;
            last_timg.tlut_type = last_tlut.type;
        }
        else
            pipeline_crashed = true; // TODO this isn't a real crash but it should warn
    }
    return 0;
}

static int
tlut_handler (uint32_t tlut, int32_t idx, int32_t count)
{
    last_tlut.tlut = segmented_to_physical(tlut);
    last_tlut.idx = idx;
    last_tlut.count = count;

    return 0;
}

static int
vtx_handler (uint32_t vtx, int32_t num)
{
    last_vtx.vtx = segmented_to_physical(vtx);
    last_vtx.num = num;

    return 0;
}

/**************************************************************************
 *  
 */

static void
arg_handler (int arg_num)
{
/*     int arg_type = gfxd_arg_type(arg_num);
    int arg_fmt = gfxd_arg_fmt(arg_num);
    const gfxd_value_t *gfxd_arg_value(int arg_num);
    const gfxd_value_t *gfxd_value_by_type(int type, int idx);
    int gfxd_arg_valid(int arg_num); */
    int arg_type = gfxd_arg_type(arg_num);
    int arg_fmt = gfxd_arg_fmt(arg_num);
    const gfxd_value_t *arg_value = gfxd_arg_value(arg_num);
    int value;

    /* validations */
    switch (arg_type)
    {
        case gfxd_Fmt:
            switch (arg_value->i)
            {
                case G_IM_FMT_CI:
                case G_IM_FMT_I:
                case G_IM_FMT_IA:
                case G_IM_FMT_RGBA:
                case G_IM_FMT_YUV:
                    break;
                default:
                    pipeline_crashed = true;
                    break;
            }
            break;
        case gfxd_Cimg:
        case gfxd_Zimg:
            if (segmented_to_physical(arg_value->u) % 0x40 != 0)
                pipeline_crashed = true;
        case gfxd_Timg:
        case gfxd_Tlut:
        case gfxd_Vtxptr:
        case gfxd_Dl:
        case gfxd_Mtxptr:
        case gfxd_Uctext:
        case gfxd_Ucdata:
        case gfxd_Lookatptr:
        case gfxd_Segptr:
        case gfxd_Lightsn:
        case gfxd_Lightptr:
        case gfxd_Vpptr:
        case gfxd_SpritePtr:
        case gfxd_BgPtr:
        case gfxd_ObjMtxptr:
        case gfxd_ObjTxtr:
        case gfxd_ObjTxSprite:
            if (!addr_in_rdram(arg_value->u))
                pipeline_crashed = true;
            break;
        case gfxd_Vtx:
            // TODO this triggers for G_LINE3D, but this is a No-Op when f3dex2 is loaded, so this can't be a crash

            // trying to index a vertex out of bounds
            // TODO track current number of loaded vertices and use that as the cap instead
            if (arg_value->i >= 32)
                pipeline_crashed = true;
            break;
    }

    if (!gfxd_arg_valid(arg_num))
        pipeline_crashed = true;

    /* additional printing */
    switch (arg_type)
    {
        case gfxd_Tlut:
        case gfxd_Timg:
        case gfxd_Cimg:
        case gfxd_Zimg:
        case gfxd_Dl:
        case gfxd_Mtxptr:
        case gfxd_Uctext:
        case gfxd_Ucdata:
        case gfxd_Lookatptr:
        case gfxd_Segptr:
        case gfxd_Lightsn:
        case gfxd_Lightptr:
        case gfxd_Vtxptr:
        case gfxd_Vpptr:
        case gfxd_SpritePtr:
        case gfxd_BgPtr:
        case gfxd_ObjMtxptr:
        case gfxd_ObjTxtr:
        case gfxd_ObjTxSprite:
            if (gfxd_arg_callbacks(arg_num) == 0)
            {
                uint32_t addr_conv = segmented_to_kseg0(arg_value->u);
                gfxd_print_value(arg_type, arg_value);
                if (addr_conv != arg_value->u)
                    gfxd_printf(" /* 0x%08X */", addr_conv);
            }
            break;
        default:
            gfxd_arg_dflt(arg_num);
            break;
    }
}

/**************************************************************************
 *  
 */

int
analyze_gbi (const char *file_name, uint32_t start_addr)
{
    if (rdram_open(file_name))
        goto err; // TODO error message

    if (start_addr == 0xFFFFFFFF)
    {
        uint32_t auto_start_addr;

        if (rdram_seek(0x12D260) || rdram_read(&auto_start_addr, sizeof(uint32_t), 1) != 1)
            goto err; // TODO error message

        start_addr = __builtin_bswap32(auto_start_addr);
    }

    start_addr &= ~KSEG_MASK;

    if (rdram_seek(start_addr))
        goto err; // TODO error message

    gfx_addr = start_addr;

    gfxd_input_callback(input_callback);
    gfxd_output_fd(fileno(stdout));

    gfxd_endian(gfxd_endian_big, 4);
    gfxd_disable(gfxd_stop_on_invalid);
    gfxd_macro_fn(macro_fn);
    gfxd_arg_fn(arg_handler);
    gfxd_dl_callback(dl_handler);
    gfxd_seg_callback(seg_handler);
    gfxd_uctext_callback(uctext_handler);
    gfxd_cimg_callback(cimg_handler);
    gfxd_timg_callback(timg_handler);
    gfxd_tlut_callback(tlut_handler);

    gfxd_vtx_callback(vtx_handler);

    next_ucode = gfxd_f3dex2; 
    gfxd_target(gfxd_f3dex2);

    n_gfx = 0;
    while (!task_done && !pipeline_crashed && gfxd_execute() == 1)
    {
        gfx_addr += last_macro_size;
        n_gfx++;

        if (last_timg.timg != 0)
        {
            draw_last_timg();
            memset(&last_timg, 0, sizeof(last_timg));
        }
        if (last_vtx.vtx != 0)
        {
            print_last_vtx();

            last_vtx.vtx = 0;
            last_vtx.num = 0;
        }
        if (last_mtx.mtx != 0)
        {
            print_last_mtx();

            last_mtx.mtx = 0;
            last_mtx.params = 0;
        }
        // TODO attempt an actually good buffer overflow check?
/*         if (i > 0x2460)
        {
            printf("Buffer overflow!\n");
            pipeline_crashed = true;
        } */
        gfxd_target(next_ucode);
    }

    if (pipeline_crashed)
    {
        printf("Graphics Task has crashed!\n");

        printf("Segment dump:\n");
        print_segments();

        if (last_open_disps.file != 0)
        {
            printf("Last OPEN_DISPS: ");
            print_string_at_addr(last_open_disps.file, printf);
            printf(", %d\n", last_open_disps.line);
        }

        printf("Last othermode:\n");
        print_othermode();
        printf("\n");

        printf("Last geometrymode:\n");
        print_geometrymode();
        printf("\n");

        printf("Last CC:\n");
        print_cc();
        printf("\n");

        // printf("Last mtx:\n");
        // print_last_mtx();
        // printf("\n");
    }
    else if (task_done)
        printf("Graphics Task completed without error.\n");

    // TODO output more information here
    // for verbose outputs, the maximum depth reached on the DL stack might be useful, also
    //  the percentage of the gfxpool that gets used by this graphics task
    // also output any warnings and what the cause of the crash is if applicable

    rdram_close();
    return 0;
err:
    rdram_close();
    return -1;
}
