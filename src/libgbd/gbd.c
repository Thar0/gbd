#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gfx.h"
#include "libgbd/gbd.h"
#include "vector.h"
#include "obstack.h"
#include "macros.h"
#include "vt.h"

#ifdef WINDOWS
# include "libiconv/windows/include/iconv.h"
#else
# include "libiconv/linux/include/iconv.h"
#endif

#define KSEG_MASK (0b111 << 29)

struct bl_conf
{
    // Cycle 1
    int m1a_c1;
    int m1b_c1;
    int m2a_c1;
    int m2b_c1;
    // Cycle 2
    int m1a_c2;
    int m1b_c2;
    int m2a_c2;
    int m2b_c2;
};

static void
bl_decode (struct bl_conf *out, uint32_t rm)
{
    out->m1a_c1 = (rm >> 0x1E) & 3;
    out->m1b_c1 = (rm >> 0x1A) & 3;
    out->m2a_c1 = (rm >> 0x16) & 3;
    out->m2b_c1 = (rm >> 0x12) & 3;

    out->m1a_c2 = (rm >> 0x1C) & 3;
    out->m1b_c2 = (rm >> 0x18) & 3;
    out->m2a_c2 = (rm >> 0x14) & 3;
    out->m2b_c2 = (rm >> 0x10) & 3;
}

struct cc_conf
{
    // cycle 1 rgb
    uint8_t a0;
    uint8_t b0;
    uint8_t c0;
    uint8_t d0;
    // cycle 1 alpha
    uint8_t Aa0;
    uint8_t Ab0;
    uint8_t Ac0;
    uint8_t Ad0;
    // cycle 2 rgb
    uint8_t a1;
    uint8_t b1;
    uint8_t c1;
    uint8_t d1;
    // cycle 2 alpha
    uint8_t Aa1;
    uint8_t Ab1;
    uint8_t Ac1;
    uint8_t Ad1;
};

static void
cc_decode (struct cc_conf *out, uint32_t hi, uint32_t lo)
{
    out->a0  = SHIFTR(hi, 4, 20);
    out->c0  = SHIFTR(hi, 5, 15);
    out->Aa0 = SHIFTR(hi, 3, 12);
    out->Ac0 = SHIFTR(hi, 3,  9);
    out->a1  = SHIFTR(hi, 4,  5);
    out->c1  = SHIFTR(hi, 5,  0);

    out->b0  = SHIFTR(lo, 4, 28);
    out->b1  = SHIFTR(lo, 4, 24);
    out->Aa1 = SHIFTR(lo, 3, 21);
    out->Ac1 = SHIFTR(lo, 3, 18);
    out->d0  = SHIFTR(lo, 3, 15);
    out->Ab0 = SHIFTR(lo, 3, 12);
    out->Ad0 = SHIFTR(lo, 3,  9);
    out->d1  = SHIFTR(lo, 3,  6);
    out->Ab1 = SHIFTR(lo, 3,  3);
    out->Ad1 = SHIFTR(lo, 3,  0);
}

typedef struct
{
    // gDPSetTile
    int fmt;            /* texture image format */
    int siz;            /* pixel size */
    int line;           /* Size of one row (s) of the texture tile (9-bit) */
    uint16_t tmem;      /* tmem address of texture tile origin (9-bit) */
    int palette;        /* position of palette for 4-bit CI (4-bit) */
    unsigned cms;       /* s-axis flags */
    unsigned cmt;       /* t-axis flags */
    int masks;          /* s-axis mask */
    int maskt;          /* t-axis mask */
    int shifts;         /* s-axis shift */
    int shiftt;         /* t-axis shift */
    // gDPSetTileSize
    qu102_t uls;        /* tile upper-left s-coordinate */
    qu102_t ult;        /* tile upper-left t-coordinate */
    qu102_t lrs;        /* tile lower-right s-coordinate */
    qu102_t lrt;        /* tile lower-right t-coordinate */
} tile_descriptor_t;

typedef struct {
    uint32_t str_addr;
    int line_no;
} DispEntry;

#define VTX_CACHE_SIZE 32

typedef struct
{
    // Options
    gbd_options_t *options;

    // Task
    gfx_ucode_registry_t *ucodes;
    gfxd_ucode_t next_ucode;
    long gfx_addr;
    int n_gfx;
    bool task_done;
    bool pipeline_crashed;
    int multi_packet;
    char multi_packet_name[32];
    ObStack disp_stack;

    // RSP
    uint16_t segment_set_bits;
    uint32_t segment_table[16];
    int last_gfx_pkt_count;
    int render_tile;
    bool render_tile_on;
    int render_tile_level;
    uint16_t tex_s_scale;
    uint16_t tex_t_scale;
    MtxF projection_mtx;
    MtxF mvp_mtx;
    float persp_norm;
    uint32_t geometry_mode;
    uint32_t dl_stack[18];
    int dl_stack_top;
    Vp cur_vp;
    int last_loaded_vtx_num;
    ObStack mtx_stack;
    int matrix_stack_depth;
    bool matrix_projection_set;
    bool matrix_modelview_set;
    uint32_t sp_dram_stack_size;

    int vtx_clipcodes[VTX_CACHE_SIZE];
    uint16_t vtx_depths[VTX_CACHE_SIZE];
    float vtx_w[VTX_CACHE_SIZE];

    int ex3_mat_cull_mode;

    // RDP
    tile_descriptor_t tile_descriptors[8];
    struct
    {
        qu102_t ulx;
        qu102_t uly;
        qu102_t lrx;
        qu102_t lry;
    } scissor;
    bool scissor_set;
    bool cimg_scissor_valid;
    uint32_t othermode_hi;
    uint32_t othermode_lo;
    struct bl_conf bl;
    uint32_t combiner_hi;
    uint32_t combiner_lo;
    struct cc_conf cc;
    bool pipe_busy;
    unsigned char tile_busy[8];
    bool load_busy;
    bool fullsync;
    struct
    {
        uint32_t addr;
    } last_zimg;
    bool zimg_set;
    struct
    {
        int fmt;
        int siz;
        uint32_t width;
        uint32_t addr;
    } last_cimg;
    bool cimg_set;
    struct
    {
        int fmt;
        int siz;
        uint32_t width;
        uint32_t addr;
    } last_timg;
    uint32_t fill_color;
    bool fill_color_set;

    // RDRAM
    rdram_interface_t *rdram;
} gfx_state_t;

#define CLIP_NEGX   (1 << 0)
#define CLIP_POSX   (1 << 1)
#define CLIP_X      (CLIP_NEGX | CLIP_POSX)
#define CLIP_NEGY   (1 << 2)
#define CLIP_POSY   (1 << 3)
#define CLIP_Y      (CLIP_NEGY | CLIP_POSY)
#define CLIP_W      (1 << 4)
#define CLIP_ALL    (CLIP_NEGX | CLIP_POSX | CLIP_NEGY | CLIP_POSY | CLIP_W)

#define OTHERMODE_VAL(state, hi_lo, field) \
    ((state)->othermode_##hi_lo & MDMASK(field))

static inline tile_descriptor_t *
get_tile_desc (gfx_state_t *state, int tile)
{
    if ((unsigned)tile >= ARRAY_COUNT(state->tile_descriptors)) {
        // invalid tile descriptor
        return NULL;
    }
    return &state->tile_descriptors[tile];
}

// TODO enable/disable certain errors
#define DEFINE_WARNING(id, string) \
    GW_##id,
#define DEFINE_ERROR(id, string) \
    GW_##id,
enum gbi_warning
{
#include "warnings_errors.h"
};
#undef DEFINE_WARNING
#undef DEFINE_ERROR

#define DEFINE_WARNING(id, string) \
    [ GW_##id ] = string,
#define DEFINE_ERROR(id, string) \
    [ GW_##id ] = string,
static const char * const warn_strings[] =
{
#include "warnings_errors.h"
};
#undef DEFINE_WARNING
#undef DEFINE_ERROR

#define DEFINE_WARNING(id, string) \
    [ GW_##id ] = false,
#define DEFINE_ERROR(id, string) \
    [ GW_##id ] = true,
static const uint8_t warn_is_error[] =
{
#include "warnings_errors.h"
};
#undef DEFINE_WARNING
#undef DEFINE_ERROR

// collections of warnings

enum gbi_warning_classes
{
    GWC_MISSING_SYNCS,

};
static enum gbi_warning warn_classes[][8] =
{
    [GWC_MISSING_SYNCS] = { GW_MISSING_PIPESYNC, GW_MISSING_LOADSYNC, GW_MISSING_TILESYNC },
};

// warning flags

static const char * const warn_flags[] =
{
    [GWC_MISSING_SYNCS] = "missing-syncs",

};

typedef int (*print_fn)(const char *fmt, ...);
typedef int (*fprint_fn)(FILE *file, const char *fmt, ...);
typedef int (*vprint_fn)(const char*fmt, va_list args);

static inline void
_Vprint (vprint_fn vpfn, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    vpfn(fmt, args);

    va_end(args);
}

/*
note: in expansion of macro '<NAME>'
warning: <desc> [-W<warn>]
    <more info if applicable>
    <explanation if verbose?>


note: in expansion of macro '<NAME>'
error: <desc>
    <more info if applicable>
    <explanation if verbose?>
*/

#define ERROR_COLOR     VT_RGBFCOL(232,  56,  40)
#define WARNING_COLOR   VT_RGBFCOL(180,   0, 160)
#define NOTE_COLOR      VT_RGBFCOL( 96, 216, 216)

void
Note (vprint_fn vpfn, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    _Vprint(vpfn, NOTE_COLOR "Note: " VT_RST);
    vpfn(fmt, args);
    _Vprint(vpfn, "\n");

    va_end(args);
}

// TODO enabling/disabling warnings
#define warning_disabled(id) false

void
Warning_Error (gfx_state_t *state, vprint_fn vpfn, enum gbi_warning warn_id, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    bool err = warn_is_error[warn_id];

    if (err || !warning_disabled(warn_id))
    {
        if (state->multi_packet)
        {
            Note(vpfn, "In expansion of macro '%s':", state->multi_packet_name);
            _Vprint(vpfn, "  ");
        }

        _Vprint(vpfn, (err) ? (ERROR_COLOR "Error: " VT_RST) : (WARNING_COLOR "Warning: " VT_RST));
        vpfn(fmt, args);
        _Vprint(vpfn, "\n");
    }

    va_end(args);

    if (err)
        state->pipeline_crashed = true;
}

#define WARNING_ERROR(state, reason, ...) \
    Warning_Error(state, gfxd_vprintf, (reason), warn_strings[(reason)], ##__VA_ARGS__)

#define ARG_CHECK(state, cond, reason, ...) \
    (cond) ? (void)0 : WARNING_ERROR(state, reason, ##__VA_ARGS__)

static inline int
sign_extend (int v, int n)
{
    unsigned mask = (1 << (n-1)) - 1;
    unsigned sgnbit = (1 << (n-1));

    return (v & mask) - (v & sgnbit);
}

int
gfx_fprintf_wrapper (FILE *file, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    int ret = gfxd_vprintf(fmt, args);

    va_end(args);
    return ret;
}

void
print_string (gfx_state_t *state, uint32_t str_addr, fprint_fn pfn, FILE *file)
{
    iconv_t cd;
    char c;
    char *in_buf, *out_buf, *iconv_in_buf, *iconv_out_buf;
    size_t in_bytes_tot, out_bytes_max, in_bytes_left, out_bytes_left;
    size_t str_len = 0;

    /* determine the length of the string */
    state->rdram->seek(str_addr);
    do {
        if (state->rdram->read(&c, sizeof(c), 1) != 1)
            return;
        str_len++;

        if (str_len > 1000) {
            pfn(file, "/*no \\0 ?*/");
            str_len = 10;
            break;
        }
    } while (c != '\0');

    /* open string */
    pfn(file, "\"");

    /* read the whole string */
    in_buf = malloc(sizeof(char) * str_len);
    out_buf = malloc(sizeof(wchar_t) * str_len);

    state->rdram->seek(str_addr);
    if (state->rdram->read(in_buf, sizeof(char), str_len) != str_len)
        goto err;

    /* convert string from EUC-JP to UTF-8 */
    cd = iconv_open("UTF-8", "EUC-JP");

    in_bytes_tot = sizeof(char) * str_len;
    out_bytes_max = sizeof(wchar_t) * str_len;
    in_bytes_left = in_bytes_tot;
    out_bytes_left = out_bytes_max;
    iconv_in_buf = in_buf;
    iconv_out_buf = out_buf;
    iconv(cd, &iconv_in_buf, &in_bytes_left, &iconv_out_buf, &out_bytes_left);
    iconv_close(cd);

    /* print converted string */
    pfn(file, "%.*s", (int)(out_bytes_max - out_bytes_left), out_buf);

err:
    /* close string */
    pfn(file, "\"");

    free(in_buf);
    free(out_buf);
}

int
print_light (FILE *print_out, gfx_state_t *state, uint32_t lights_addr, int count)
{
    Lightsn lights;

    if (!state->rdram->read_at(&lights, lights_addr, sizeof(Ambient) + count * sizeof(Light)))
        goto err;

    fprintf(print_out, "(Ambient){\n");
    fprintf(print_out, "    .col  = { %3d, %3d, %3d },\n", lights.a.l.col[0],  lights.a.l.col[1],  lights.a.l.col[2]);
    fprintf(print_out, "    .colc = { %3d, %3d, %3d },\n", lights.a.l.colc[0], lights.a.l.colc[1], lights.a.l.colc[2]);
    fprintf(print_out, "},\n");

    fprintf(print_out, "/* Lights%d */\n", count);
    for (int i = 0; i < count; i++)
    {
        fprintf(print_out, "(Light){\n");
        fprintf(print_out, "    .col  = { %3d, %3d, %3d },\n",
                lights.l[i].l.col[0],  lights.l[i].l.col[1],  lights.l[i].l.col[2]);
        fprintf(print_out, "    .pad1 = %d,\n", lights.l[i].l.pad1);
        fprintf(print_out, "    .colc = { %3d, %3d, %3d },\n",
                lights.l[i].l.colc[0], lights.l[i].l.colc[1], lights.l[i].l.colc[2]);
        fprintf(print_out, "    .pad2 = %d,\n", lights.l[i].l.pad2);
        fprintf(print_out, "    .dir  = { %3d, %3d, %3d },\n",
                lights.l[i].l.dir[0],  lights.l[i].l.dir[1],  lights.l[i].l.dir[2]);
        fprintf(print_out, "    .pad3 = %d,\n", lights.l[i].l.pad3);
        fprintf(print_out, "},\n");
    }

    return 0;
err:
    fprintf(print_out, VT_RGBCOL(255, 0, 0, 255, 255, 255) "READ ERROR" VT_RST "\n");
    return -1;
}

static const char *
cc_str (unsigned int v, int i)
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

void
print_cc (FILE *print_out, uint32_t cc_hi, uint32_t cc_lo)
{
    fprintf(print_out, "%s, %s, %s, %s, "
                       "%s, %s, %s, %s, "
                       "%s, %s, %s, %s, "
                       "%s, %s, %s, %s",
            cc_str(SHIFTR(cc_hi, 4, 20), 0),
            cc_str(SHIFTR(cc_lo, 4, 28), 1),
            cc_str(SHIFTR(cc_hi, 5, 15), 2),
            cc_str(SHIFTR(cc_lo, 3, 15), 3),
            cc_str(SHIFTR(cc_hi, 3, 12), 4),
            cc_str(SHIFTR(cc_lo, 3, 12), 5),
            cc_str(SHIFTR(cc_hi, 3, 9),  6),
            cc_str(SHIFTR(cc_lo, 3, 9),  7),
            cc_str(SHIFTR(cc_hi, 4, 5),  8),
            cc_str(SHIFTR(cc_lo, 4, 24), 9),
            cc_str(SHIFTR(cc_hi, 5, 0),  10),
            cc_str(SHIFTR(cc_lo, 3, 6),  11),
            cc_str(SHIFTR(cc_lo, 3, 21), 12),
            cc_str(SHIFTR(cc_lo, 3, 3),  13),
            cc_str(SHIFTR(cc_lo, 3, 18), 14),
            cc_str(SHIFTR(cc_lo, 3, 0),  15));
}

void
print_geometrymode (FILE *print_out, uint32_t geometry_mode)
{
    struct F3DZEX_const geometry_modes[] =
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

    for (size_t i = 0; i < ARRAY_COUNT(geometry_modes); i++)
    {
        if ((geometry_modes[i].value & geometry_mode) == 0)
            continue;

        if (!first)
            fprintf(print_out, " | ");

        fprintf(print_out, "%s", geometry_modes[i].name);
        geometry_mode &= ~geometry_modes[i].value;
        first = false;
    }
    if (geometry_mode != 0)
    {
        if (!first)
            fprintf(print_out, " | ");
        fprintf(print_out, "0x%08X", geometry_mode);
    }
    else if (first)
        fprintf(print_out, "0");
}

static void
print_othermode_hi (FILE *print_out, uint32_t othermode_hi)
{
#define CASE_PRINT(name) \
    case name : fprintf(print_out, #name); break
#define DEFAULT_PRINT(name) \
    default: fprintf(print_out, "0x%08X", othermode_hi & MDMASK(name)); break

    switch (othermode_hi & MDMASK(ALPHADITHER))
    {
        CASE_PRINT(G_AD_PATTERN);
        CASE_PRINT(G_AD_NOTPATTERN);
        CASE_PRINT(G_AD_NOISE);
        CASE_PRINT(G_AD_DISABLE);
        DEFAULT_PRINT(ALPHADITHER);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(RGBDITHER))
    {
        CASE_PRINT(G_CD_MAGICSQ);
        CASE_PRINT(G_CD_BAYER);
        CASE_PRINT(G_CD_NOISE);
        CASE_PRINT(G_CD_DISABLE);
        DEFAULT_PRINT(RGBDITHER);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(COMBKEY))
    {
        CASE_PRINT(G_CK_NONE);
        CASE_PRINT(G_CK_KEY);
        DEFAULT_PRINT(COMBKEY);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(TEXTCONV))
    {
        CASE_PRINT(G_TC_CONV);
        CASE_PRINT(G_TC_FILTCONV);
        CASE_PRINT(G_TC_FILT);
        DEFAULT_PRINT(TEXTCONV);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(TEXTFILT))
    {
        CASE_PRINT(G_TF_POINT);
        CASE_PRINT(G_TF_BILERP);
        CASE_PRINT(G_TF_AVERAGE);
        DEFAULT_PRINT(TEXTFILT);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(TEXTLUT))
    {
        CASE_PRINT(G_TT_NONE);
        CASE_PRINT(G_TT_RGBA16);
        CASE_PRINT(G_TT_IA16);
        DEFAULT_PRINT(TEXTLUT);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(TEXTLOD))
    {
        CASE_PRINT(G_TL_TILE);
        CASE_PRINT(G_TL_LOD);
        DEFAULT_PRINT(TEXTLOD);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(TEXTDETAIL))
    {
        CASE_PRINT(G_TD_CLAMP);
        CASE_PRINT(G_TD_SHARPEN);
        CASE_PRINT(G_TD_DETAIL);
        DEFAULT_PRINT(TEXTDETAIL);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(TEXTPERSP))
    {
        CASE_PRINT(G_TP_NONE);
        CASE_PRINT(G_TP_PERSP);
        DEFAULT_PRINT(TEXTPERSP);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(CYCLETYPE))
    {
        CASE_PRINT(G_CYC_1CYCLE);
        CASE_PRINT(G_CYC_2CYCLE);
        CASE_PRINT(G_CYC_COPY);
        CASE_PRINT(G_CYC_FILL);
        DEFAULT_PRINT(CYCLETYPE);
    }
    fprintf(print_out, " | ");
    switch (othermode_hi & MDMASK(PIPELINE))
    {
        CASE_PRINT(G_PM_NPRIMITIVE);
        CASE_PRINT(G_PM_1PRIMITIVE);
        DEFAULT_PRINT(PIPELINE);
    }
    uint32_t unk_mask = ~(MDMASK(ALPHADITHER) | MDMASK(RGBDITHER) | MDMASK(COMBKEY) | MDMASK(TEXTCONV)
                        | MDMASK(TEXTFILT) | MDMASK(TEXTLUT) | MDMASK(TEXTLOD) | MDMASK(TEXTDETAIL)
                        | MDMASK(TEXTPERSP) | MDMASK(CYCLETYPE) | MDMASK(PIPELINE));
    if (othermode_hi & unk_mask)
    {
        fprintf(print_out, "| 0x%08X", othermode_hi & unk_mask);
    }
#undef CASE_PRINT
#undef DEFAULT_PRINT
}

static int
print_rm_mode (FILE *print_out, uint32_t arg)
{
    int n = 0;

    if (arg & AA_EN)
        n += fprintf(print_out, "AA_EN");

    if (arg & Z_CMP)
    {
        if (n > 0)
            n += fprintf(print_out, " | ");
        n += fprintf(print_out, "Z_CMP");
    }

    if (arg & Z_UPD)
    {
        if (n > 0)
            n += fprintf(print_out, " | ");
        n += fprintf(print_out, "Z_UPD");
    }

    if (arg & IM_RD)
    {
        if (n > 0)
            n += fprintf(print_out, " | ");
        n += fprintf(print_out, "IM_RD");
    }

    if (arg & CLR_ON_CVG)
    {
        if (n > 0)
            n += fprintf(print_out, " | ");
        n += fprintf(print_out, "CLR_ON_CVG");
    }

    if (n > 0)
        n += fprintf(print_out, " | ");

    switch (arg & 0x00000300)
    {
        case CVG_DST_CLAMP: n += fprintf(print_out, "CVG_DST_CLAMP"); break;
        case CVG_DST_WRAP:  n += fprintf(print_out, "CVG_DST_WRAP");  break;
        case CVG_DST_FULL:  n += fprintf(print_out, "CVG_DST_FULL");  break;
        case CVG_DST_SAVE:  n += fprintf(print_out, "CVG_DST_SAVE");  break;
    }
    switch (arg & 0x00000C00)
    {
        case ZMODE_OPA:   n += fprintf(print_out, " | ZMODE_OPA"); break;
        case ZMODE_INTER: n += fprintf(print_out, " | ZMODE_INTER"); break;
        case ZMODE_XLU:   n += fprintf(print_out, " | ZMODE_XLU"); break;
        case ZMODE_DEC:   n += fprintf(print_out, " | ZMODE_DEC"); break;
    }

    if (arg & CVG_X_ALPHA)
        n += fprintf(print_out, " | CVG_X_ALPHA");

    if (arg & ALPHA_CVG_SEL)
        n += fprintf(print_out, " | ALPHA_CVG_SEL");

    if (arg & FORCE_BL)
        n += fprintf(print_out, " | FORCE_BL");

    return n;
}

static int
print_rm_cbl (FILE *print_out, uint32_t arg, int c)
{
    int n = 0;
    if (c == 2)
        arg <<= 2;

    switch ((arg >> 30) & 0b11)
    {
        case G_BL_CLR_IN:  n += fprintf(print_out, "GBL_c%i(G_BL_CLR_IN", c);  break;
        case G_BL_CLR_MEM: n += fprintf(print_out, "GBL_c%i(G_BL_CLR_MEM", c); break;
        case G_BL_CLR_BL:  n += fprintf(print_out, "GBL_c%i(G_BL_CLR_BL", c);  break;
        case G_BL_CLR_FOG: n += fprintf(print_out, "GBL_c%i(G_BL_CLR_FOG", c); break;
    }
    switch ((arg >> 26) & 0b11)
    {
        case G_BL_A_IN:    n += fprintf(print_out, ", G_BL_A_IN");    break;
        case G_BL_A_FOG:   n += fprintf(print_out, ", G_BL_A_FOG");   break;
        case G_BL_A_SHADE: n += fprintf(print_out, ", G_BL_A_SHADE"); break;
        case G_BL_0:       n += fprintf(print_out, ", G_BL_0");       break;
    }
    switch ((arg >> 22) & 0b11)
    {
        case G_BL_CLR_IN:  n += fprintf(print_out, ", G_BL_CLR_IN");  break;
        case G_BL_CLR_MEM: n += fprintf(print_out, ", G_BL_CLR_MEM"); break;
        case G_BL_CLR_BL:  n += fprintf(print_out, ", G_BL_CLR_BL");  break;
        case G_BL_CLR_FOG: n += fprintf(print_out, ", G_BL_CLR_FOG"); break;
    }
    switch ((arg >> 18) & 0b11)
    {
        case G_BL_1MA:   n += fprintf(print_out, ", G_BL_1MA)");   break;
        case G_BL_A_MEM: n += fprintf(print_out, ", G_BL_A_MEM)"); break;
        case G_BL_1:     n += fprintf(print_out, ", G_BL_1)");     break;
        case G_BL_0:     n += fprintf(print_out, ", G_BL_0)");     break;
    }
    return n;
}

static void
print_othermode_lo (FILE *print_out, uint32_t othermode_lo)
{
    static const struct F3DZEX_const rm_presets[] =
    {
        F3DZEX_CONST(G_RM_OPA_SURF),
        F3DZEX_CONST(G_RM_OPA_SURF2),
        F3DZEX_CONST(G_RM_AA_OPA_SURF),
        F3DZEX_CONST(G_RM_AA_OPA_SURF2),
        F3DZEX_CONST(G_RM_RA_OPA_SURF),
        F3DZEX_CONST(G_RM_RA_OPA_SURF2),
        F3DZEX_CONST(G_RM_ZB_OPA_SURF),
        F3DZEX_CONST(G_RM_ZB_OPA_SURF2),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_SURF),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_SURF2),
        F3DZEX_CONST(G_RM_RA_ZB_OPA_SURF),
        F3DZEX_CONST(G_RM_RA_ZB_OPA_SURF2),
        F3DZEX_CONST(G_RM_XLU_SURF),
        F3DZEX_CONST(G_RM_XLU_SURF2),
        F3DZEX_CONST(G_RM_AA_XLU_SURF),
        F3DZEX_CONST(G_RM_AA_XLU_SURF2),
        F3DZEX_CONST(G_RM_ZB_XLU_SURF),
        F3DZEX_CONST(G_RM_ZB_XLU_SURF2),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_SURF),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_SURF2),
        F3DZEX_CONST(G_RM_ZB_OPA_DECAL),
        F3DZEX_CONST(G_RM_ZB_OPA_DECAL2),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_DECAL),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_DECAL2),
        F3DZEX_CONST(G_RM_RA_ZB_OPA_DECAL),
        F3DZEX_CONST(G_RM_RA_ZB_OPA_DECAL2),
        F3DZEX_CONST(G_RM_ZB_XLU_DECAL),
        F3DZEX_CONST(G_RM_ZB_XLU_DECAL2),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_DECAL),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_DECAL2),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_INTER),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_INTER2),
        F3DZEX_CONST(G_RM_RA_ZB_OPA_INTER),
        F3DZEX_CONST(G_RM_RA_ZB_OPA_INTER2),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_INTER),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_INTER2),
        F3DZEX_CONST(G_RM_AA_XLU_LINE),
        F3DZEX_CONST(G_RM_AA_XLU_LINE2),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_LINE),
        F3DZEX_CONST(G_RM_AA_ZB_XLU_LINE2),
        F3DZEX_CONST(G_RM_AA_DEC_LINE),
        F3DZEX_CONST(G_RM_AA_DEC_LINE2),
        F3DZEX_CONST(G_RM_AA_ZB_DEC_LINE),
        F3DZEX_CONST(G_RM_AA_ZB_DEC_LINE2),
        F3DZEX_CONST(G_RM_TEX_EDGE),
        F3DZEX_CONST(G_RM_TEX_EDGE2),
        F3DZEX_CONST(G_RM_AA_TEX_EDGE),
        F3DZEX_CONST(G_RM_AA_TEX_EDGE2),
        F3DZEX_CONST(G_RM_AA_ZB_TEX_EDGE),
        F3DZEX_CONST(G_RM_AA_ZB_TEX_EDGE2),
        F3DZEX_CONST(G_RM_AA_ZB_TEX_INTER),
        F3DZEX_CONST(G_RM_AA_ZB_TEX_INTER2),
        F3DZEX_CONST(G_RM_AA_SUB_SURF),
        F3DZEX_CONST(G_RM_AA_SUB_SURF2),
        F3DZEX_CONST(G_RM_AA_ZB_SUB_SURF),
        F3DZEX_CONST(G_RM_AA_ZB_SUB_SURF2),
        F3DZEX_CONST(G_RM_PCL_SURF),
        F3DZEX_CONST(G_RM_PCL_SURF2),
        F3DZEX_CONST(G_RM_AA_PCL_SURF),
        F3DZEX_CONST(G_RM_AA_PCL_SURF2),
        F3DZEX_CONST(G_RM_ZB_PCL_SURF),
        F3DZEX_CONST(G_RM_ZB_PCL_SURF2),
        F3DZEX_CONST(G_RM_AA_ZB_PCL_SURF),
        F3DZEX_CONST(G_RM_AA_ZB_PCL_SURF2),
        F3DZEX_CONST(G_RM_AA_OPA_TERR),
        F3DZEX_CONST(G_RM_AA_OPA_TERR2),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_TERR),
        F3DZEX_CONST(G_RM_AA_ZB_OPA_TERR2),
        F3DZEX_CONST(G_RM_AA_TEX_TERR),
        F3DZEX_CONST(G_RM_AA_TEX_TERR2),
        F3DZEX_CONST(G_RM_AA_ZB_TEX_TERR),
        F3DZEX_CONST(G_RM_AA_ZB_TEX_TERR2),
        F3DZEX_CONST(G_RM_AA_SUB_TERR),
        F3DZEX_CONST(G_RM_AA_SUB_TERR2),
        F3DZEX_CONST(G_RM_AA_ZB_SUB_TERR),
        F3DZEX_CONST(G_RM_AA_ZB_SUB_TERR2),
        F3DZEX_CONST(G_RM_CLD_SURF),
        F3DZEX_CONST(G_RM_CLD_SURF2),
        F3DZEX_CONST(G_RM_ZB_CLD_SURF),
        F3DZEX_CONST(G_RM_ZB_CLD_SURF2),
        F3DZEX_CONST(G_RM_ZB_OVL_SURF),
        F3DZEX_CONST(G_RM_ZB_OVL_SURF2),
        F3DZEX_CONST(G_RM_ADD),
        F3DZEX_CONST(G_RM_ADD2),
        F3DZEX_CONST(G_RM_VISCVG),
        F3DZEX_CONST(G_RM_VISCVG2),
        F3DZEX_CONST(G_RM_OPA_CI),
        F3DZEX_CONST(G_RM_OPA_CI2),
        F3DZEX_CONST(G_RM_RA_SPRITE),
        F3DZEX_CONST(G_RM_RA_SPRITE2),
    };
    static const struct F3DZEX_const bl1_presets[] =
    {
        F3DZEX_CONST(G_RM_FOG_SHADE_A),
        F3DZEX_CONST(G_RM_FOG_PRIM_A),
        F3DZEX_CONST(G_RM_PASS),
        F3DZEX_CONST(G_RM_NOOP),
    };
    static const struct F3DZEX_const bl2_presets[] =
    {
        F3DZEX_CONST(G_RM_NOOP2),
    };
#define MDMASK_RM_C1	((uint32_t)0xCCCC0000)
#define MDMASK_RM_C2	((uint32_t)0x33330000)
#define MDMASK_RM_LO	((uint32_t)0x0000FFF8)
#define RM_MASK         (MDMASK_RM_C1 | MDMASK_RM_C2 | MDMASK_RM_LO)

    const struct F3DZEX_const *pre_c1 = NULL;
    const struct F3DZEX_const *pre_c2 = NULL;
    int n = 0;

    for (size_t i = 0; i < ARRAY_COUNT(rm_presets); i++)
    {
        const struct F3DZEX_const *pre = &rm_presets[i];

        uint32_t rm_c1 = othermode_lo & (MDMASK_RM_C1 | MDMASK_RM_LO | (pre->value & ~RM_MASK));
        if (!pre_c1 && rm_c1 == pre->value)
            pre_c1 = pre;

        uint32_t rm_c2 = othermode_lo & (MDMASK_RM_C2 | MDMASK_RM_LO | (pre->value & ~RM_MASK));
        if (!pre_c2 && rm_c2 == pre->value)
            pre_c2 = pre;
    }

    if (!pre_c1 || !pre_c2 || pre_c1 + 1 != pre_c2)
    {
        for (size_t i = 0; i < ARRAY_COUNT(bl1_presets); i++)
        {
            const struct F3DZEX_const *pre = &bl1_presets[i];

            uint32_t rm_c1 = othermode_lo & (MDMASK_RM_C1 | (pre->value & ~RM_MASK));
            if (rm_c1 == pre->value)
            {
                pre_c1 = pre;
                break;
            }
        }

        for (size_t i = 0; i < ARRAY_COUNT(bl2_presets); i++)
        {
            const struct F3DZEX_const *pre = &bl2_presets[i];

            uint32_t rm_c2 = othermode_lo & (MDMASK_RM_C2 | (pre->value & ~RM_MASK));
            if (rm_c2 == pre->value)
            {
                pre_c2 = pre;
                break;
            }
        }
    }

    uint32_t pre_rm = 0;

    if (pre_c1)
        pre_rm |= pre_c1->value;

    if (pre_c2)
        pre_rm |= pre_c2->value;

#define CASE_PRINT(name) \
    case name : fprintf(print_out, #name); break
#define DEFAULT_PRINT(name) \
    default: fprintf(print_out, "0x%08X", othermode_lo & MDMASK(name)); break

    switch (othermode_lo & MDMASK(ALPHACOMPARE))
    {
        CASE_PRINT(G_AC_NONE);
        CASE_PRINT(G_AC_THRESHOLD);
        CASE_PRINT(G_AC_DITHER);
        DEFAULT_PRINT(ALPHACOMPARE);
    }
    fprintf(print_out, " | ");
    switch (othermode_lo & MDMASK(ZSRCSEL))
    {
        CASE_PRINT(G_ZS_PIXEL);
        CASE_PRINT(G_ZS_PRIM);
        DEFAULT_PRINT(ZSRCSEL);
    }
#undef CASE_PRINT
#undef DEFAULT_PRINT

    uint32_t rm = othermode_lo & (RM_MASK | pre_rm);

    if ((othermode_lo & ~pre_rm) & MDMASK_RM_LO)
    {
        fprintf(print_out, " | ");
        print_rm_mode(print_out, rm);
    }

    fprintf(print_out, " | ");
    if (pre_c1)
        fprintf(print_out, "%s", pre_c1->name);
    else
        print_rm_cbl(print_out, rm, 1);

    fprintf(print_out, " | ");
    if (pre_c2)
        fprintf(print_out, "%s", pre_c2->name);
    else
        print_rm_cbl(print_out, rm, 2);

    uint32_t unk_mask = ~(RM_MASK | MDMASK(ALPHACOMPARE) | MDMASK(ZSRCSEL));
    if (othermode_lo & unk_mask)
    {
        fprintf(print_out, " | 0x%08X", othermode_lo & unk_mask);
    }
}

void
print_othermode (FILE *print_out, uint32_t othermode_hi, uint32_t othermode_lo)
{
    print_othermode_hi(print_out, othermode_hi);
    fprintf(print_out, ", ");
    print_othermode_lo(print_out, othermode_lo);
}

#define PRINT_PX(r, g, b) \
    printf(VT_RGBCOL_S("%d;%d;%d", "%d;%d;%d") "\u2584\u2584", r, g, b, r, g, b)

#define CVT_PX(c, sft, mask) \
    ((((c) >> (sft)) & (mask)) * (255/(mask)))

int
draw_last_timg (gfx_state_t *state, uint32_t timg, int fmt, int siz, int height, int width,
                uint32_t tlut, int tlut_type, int tlut_count)
{
    int fmt_siz = FMT_SIZ(fmt, siz);

    state->rdram->seek(timg);

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            switch (fmt_siz) // TODO implement transparency in some way for all of these
            {
                case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_4b):
                    {
                        uint8_t i4_px_x2;

                        if (state->rdram->read(&i4_px_x2, sizeof(uint8_t), 1) != 1)
                            goto read_err;

                        PRINT_PX(CVT_PX(i4_px_x2, 4, 15), CVT_PX(i4_px_x2, 4, 15), CVT_PX(i4_px_x2, 4, 15));
                        j++; // TODO what happens if these 4-bit textures have an odd width?
                        PRINT_PX(CVT_PX(i4_px_x2, 0, 15), CVT_PX(i4_px_x2, 0, 15), CVT_PX(i4_px_x2, 0, 15));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_4b):
                    {
                        uint8_t ia4_px_x2;

                        if (state->rdram->read(&ia4_px_x2, sizeof(uint8_t), 1) != 1)
                            goto read_err;

                        PRINT_PX(CVT_PX(ia4_px_x2, 5, 7), CVT_PX(ia4_px_x2, 5, 7), CVT_PX(ia4_px_x2, 5, 7));
                        j++;
                        PRINT_PX(CVT_PX(ia4_px_x2, 1, 7), CVT_PX(ia4_px_x2, 1, 7), CVT_PX(ia4_px_x2, 1, 7));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_4b):
                    {
                        // TODO previewing of CI4/CI8 textures is unreliable as the TLUT may be loaded after the index
                        // data
                        if (tlut == 0 || tlut_type == G_TT_NONE)
                            goto no_preview;

                        uint8_t ci8_i_x2;

                        if (state->rdram->read(&ci8_i_x2, sizeof(uint8_t), 1) != 1)
                            goto read_err;

                        uint16_t tlut_pxs[2];

                        uint32_t save_pos = state->rdram->pos();
                        state->rdram->seek(tlut + sizeof(uint16_t) * (ci8_i_x2 >> 4));
                        if (state->rdram->read(&tlut_pxs[0], sizeof(uint16_t), 1) != 1)
                            goto read_err;
                        state->rdram->seek(tlut + sizeof(uint16_t) * (ci8_i_x2 & 0xF));
                        if (state->rdram->read(&tlut_pxs[1], sizeof(uint16_t), 1) != 1)
                            goto read_err;
                        state->rdram->seek(save_pos);

                        for (int k = 0; k < 2; k++)
                        {
                            uint16_t tlut_px = BSWAP16(tlut_pxs[k]);

                            switch (tlut_type)
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

                        if (state->rdram->read(&i8_px, sizeof(uint8_t), 1) != 1)
                            goto read_err;

                        PRINT_PX(CVT_PX(i8_px, 0, 255), CVT_PX(i8_px, 0, 255), CVT_PX(i8_px, 0, 255));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_8b):
                    {
                        uint8_t ia8_px;

                        if (state->rdram->read(&ia8_px, sizeof(uint8_t), 1) != 1)
                            goto read_err;

                        PRINT_PX(CVT_PX(ia8_px, 4, 15), CVT_PX(ia8_px, 4, 15), CVT_PX(ia8_px, 4, 15));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_8b):
                    {
                        if (tlut == 0 || tlut_type == G_TT_NONE)
                            goto no_preview;

                        uint8_t ci8_i;

                        if (state->rdram->read(&ci8_i, sizeof(uint8_t), 1) != 1)
                            goto read_err;

                        // if (ci8_i > tlut_count)
                        //     goto read_err;

                        uint16_t tlut_px;

                        uint32_t save_pos = state->rdram->pos();
                        state->rdram->seek(tlut + sizeof(uint16_t) * ci8_i);
                        if (state->rdram->read(&tlut_px, sizeof(uint16_t), 1) != 1)
                            goto read_err;
                        state->rdram->seek(save_pos);

                        tlut_px = BSWAP16(tlut_px);

                        switch (tlut_type)
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

                        if (state->rdram->read(&ia16_px, sizeof(uint16_t), 1) != 1)
                            goto read_err;

                        ia16_px = BSWAP16(ia16_px);

                        // TODO most of these require the alpha channel to be visible to properly see what these look
                        // like
                        PRINT_PX(CVT_PX(ia16_px, 8, 255), CVT_PX(ia16_px, 8, 255), CVT_PX(ia16_px, 8, 255));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_16b):
                    {
                        uint16_t rgba16_px;

                        if (state->rdram->read(&rgba16_px, sizeof(uint16_t), 1) != 1)
                            goto read_err;

                        rgba16_px = BSWAP16(rgba16_px);

                        PRINT_PX(CVT_PX(rgba16_px, 11, 31), CVT_PX(rgba16_px, 6, 31), CVT_PX(rgba16_px, 1, 31));
                    }
                    break;
                case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_32b):
                    {
                        uint32_t rgba32_px;

                        if (state->rdram->read(&rgba32_px, sizeof(uint32_t), 1) != 1)
                            goto read_err;

                        rgba32_px = BSWAP32(rgba32_px);

                        PRINT_PX(CVT_PX(rgba32_px, 24, 255), CVT_PX(rgba32_px, 16, 255), CVT_PX(rgba32_px, 8, 255));
                    }
                    break;
                // TODO YUV? but who even uses YUV (it would also be more useful to show different decoding stages..)
                default:
                    goto bad_fmt_siz_err;
            }
        }
        printf(VT_RST "\n");
    }
    return 0;

no_preview:
    printf(VT_RGBCOL(255, 110, 0, 255, 255, 255) "CI texture could not be previewed" VT_RST "\n");
    return 1;

read_err:
    printf(VT_RGBCOL(255, 0, 0, 255, 255, 255) "READ ERROR" VT_RST "\n");
    printf("%08lX\n", state->rdram->pos());
    return -1;
bad_fmt_siz_err:
    printf(VT_RST);
    return -2;
}

/**************************************************************************
 *  Display List Stack
 */

static uint32_t
dl_stack_peek (gfx_state_t *state)
{
    if (state->dl_stack_top == -1)
        return 0; // peek failed, stack empty
    return state->dl_stack[state->dl_stack_top];
}

static uint32_t
dl_stack_pop (gfx_state_t *state)
{
    if (state->dl_stack_top == -1)
        return 0; // pop failed, stack empty
    return state->dl_stack[state->dl_stack_top--];
}

static int
dl_stack_push (gfx_state_t *state, uint32_t dl)
{
    if (state->dl_stack_top != ARRAY_COUNT(state->dl_stack))
        state->dl_stack[++state->dl_stack_top] = dl;
    else
        return -1; // push failed, stack full
    return 0;
}

/**************************************************************************
 *  Address Conversion and Checking
 */

void
print_segments (FILE *print_out, uint32_t *segment_table)
{
    for (int i = 0; i < 16; i++)
    {
        fprintf(print_out, "%02X : %08X", i, segment_table[i]);
        if ((i + 1) % 4 == 0)
            fprintf(print_out, "\n");
        else
            fprintf(print_out, " | ");
    }
}

static bool
addr_in_rdram (gfx_state_t *state, uint32_t addr)
{
    return state->rdram->addr_valid(addr & ~KSEG_MASK);
}

static uint32_t
segmented_to_physical (gfx_state_t *state, uint32_t addr)
{
    int num = (addr << 4) >> 28;
    uint8_t num8 = addr >> 24;

    ARG_CHECK(state, state->segment_set_bits & (1 << num), GW_UNSET_SEGMENT, num);

    if (num8 != 0x80 && num8 != 0xA0) // allow direct-mapped KSEG addresses
        ARG_CHECK(state, (addr >> 24) < 16, GW_INVALID_SEGMENT_NUM);

    // segment calculation
    return state->segment_table[num] + (addr & 0x00FFFFFF);
}

static uint32_t
segmented_to_kseg0 (gfx_state_t *state, uint32_t addr)
{
    return 0x80000000 | segmented_to_physical(state, addr);
}

/**************************************************************************
 *  Matrix Conversion
 */

static inline void
f_to_qs1616 (int16_t *int_out, uint16_t *frac_out, float f)
{
    qs1616_t q = qs1616(f);

    *int_out = (int16_t)(q >> 16);
    *frac_out = (uint16_t)(q & 0xFFFF);
}

static inline float
qs1616_to_f (int16_t int_part, uint16_t frac_part)
{
    return ((int_part << 16) | (frac_part)) / (float)0x10000;
}

void
mtxf_to_mtx (Mtx *mtx, MtxF *mf, bool swap)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
        {
            int16_t ipart;
            uint16_t fpart;

            f_to_qs1616(&ipart, &fpart, mf->mf[i][j]);

            mtx->i[i + 4 * j] = MAYBE_BSWAP16(ipart, swap);
            mtx->f[i + 4 * j] = MAYBE_BSWAP16(fpart, swap);
        }
}

void
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

void
mtxf_mtxf_mul (MtxF *dst, MtxF *m0, MtxF *m1)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        dst->mf[0][i] = m0->mf[0][i] * m1->mf[0][0] + m0->mf[1][i] * m1->mf[0][1]
                      + m0->mf[2][i] * m1->mf[0][2] + m0->mf[3][i] * m1->mf[0][3];
        dst->mf[1][i] = m0->mf[0][i] * m1->mf[1][0] + m0->mf[1][i] * m1->mf[1][1]
                      + m0->mf[2][i] * m1->mf[1][2] + m0->mf[3][i] * m1->mf[1][3];
        dst->mf[2][i] = m0->mf[0][i] * m1->mf[2][0] + m0->mf[1][i] * m1->mf[2][1]
                      + m0->mf[2][i] * m1->mf[2][2] + m0->mf[3][i] * m1->mf[2][3];
        dst->mf[3][i] = m0->mf[3][i] * m1->mf[3][3] + m0->mf[2][i] * m1->mf[3][2]
                      + m0->mf[1][i] * m1->mf[3][1] + m0->mf[0][i] * m1->mf[3][0];
    }
}

void
mtxf_mtxf_mul_inplace (MtxF *m0, MtxF *m1)
{
    MtxF tmp;
    mtxf_mtxf_mul(&tmp, m0, m1);
    *m0 = tmp;
}

/**************************************************************************
 *  Command Handlers
 */

typedef int (*chk_fn)(gfx_state_t *);

#define CHECK_PIPESYNC(state) ARG_CHECK(state, !state->pipe_busy, GW_MISSING_PIPESYNC)
#define CHECK_LOADSYNC(state) ARG_CHECK(state, !state->load_busy, GW_MISSING_LOADSYNC)
#define CHECK_TILESYNC(state, tile) ARG_CHECK(state, !state->tile_busy[(tile)], GW_MISSING_TILESYNC)

static int
chk_Range (gfx_state_t *state, uint32_t addr, size_t len)
{
    ARG_CHECK(state, addr_in_rdram(state, addr), GW_ADDR_NOT_IN_RDRAM);
    if (len != 0)
        ARG_CHECK(state, addr_in_rdram(state, addr + len), GW_RANGE_NOT_IN_RDRAM);

    return 0;
}

static bool
chk_ValidImgFmt (int fmt)
{
    switch (fmt)
    {
        case G_IM_FMT_RGBA:
        case G_IM_FMT_YUV:
        case G_IM_FMT_CI:
        case G_IM_FMT_IA:
        case G_IM_FMT_I:
            return true;
    }
    return false;
}

static bool
chk_ValidImgFmtSiz (int fmt, int siz)
{
    switch (FMT_SIZ(fmt, siz))
    {
        case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_32b):
        case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_16b):
        case FMT_SIZ(G_IM_FMT_YUV, G_IM_SIZ_16b):
        case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_16b):
        case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_8b):
        case FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_4b):
        case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_16b):    // loadblock
        case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_8b):
        case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_4b):
        case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_16b):     // loadblock
        case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_8b):
        case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_4b):
            return true;
    }
    return false;
}

static int
chk_SPBranchList (gfx_state_t *state)
{
    uint32_t dl = gfxd_arg_value(0)->u;
    uint32_t dl_phys = segmented_to_physical(state, dl);

    // We don't actually know the length of the display list being branched to,
    // but it better contain at least one command
    chk_Range(state, dl_phys, sizeof(Gfx));

    state->gfx_addr = dl_phys - sizeof(Gfx);
    return 0;
}

static int
chk_SPDisplayList (gfx_state_t *state)
{
    if (dl_stack_push(state, state->gfx_addr) == -1)
        WARNING_ERROR(state, GW_DL_STACK_OVERFLOW);

    return chk_SPBranchList(state);
}

static int
chk_DisplayList (gfx_state_t *state)
{
    int flag = gfxd_arg_value(1)->u;

    const char *acts_as = (flag & 1) ? "SPBranchList" : "SPDisplayList";
    chk_fn continue_chk = (flag & 1) ? chk_SPBranchList : chk_SPDisplayList;

    WARNING_ERROR(state, GW_UNK_DL_VARIANT, acts_as);

    return continue_chk(state);
}

static int
chk_SPEndDisplayList (gfx_state_t *state)
{
    if (state->dl_stack_top == -1) // dl stack empty, task is done
        state->task_done = true;
    else
        state->gfx_addr = dl_stack_pop(state);
    return 0;
}

static int
chk_SPCullDisplayList (gfx_state_t *state)
{
    int v0 = gfxd_arg_value(0)->i;
    int vn = gfxd_arg_value(1)->i;

    int last_loaded_vtx_num = state->last_loaded_vtx_num;

    ARG_CHECK(state, v0 < last_loaded_vtx_num && vn < last_loaded_vtx_num, GW_CULLING_BAD_VERTS);

    ARG_CHECK(state, vn > v0, GW_CULLING_BAD_INDICES);
    ARG_CHECK(state, v0 >= 0 && vn < VTX_CACHE_SIZE, GW_CULLING_VERTS_OOB);

    // Skip emulation if vertices are bad
    if (state->pipeline_crashed)
        return -1;

    int check = CLIP_ALL;

    int *startClip = &state->vtx_clipcodes[v0];
    int *endClip = &state->vtx_clipcodes[vn];

    do
    {
        check &= *startClip;
        if (check == 0)
            return 0; // at least one vertex on-screen, execute next command in display list
    }
    while((startClip++) != endClip);

    Note(gfxd_vprintf, "Display list culled");

    // no vertices on-screen, display list can be culled, end early
    return chk_SPEndDisplayList(state);
}

static int
chk_Segment (gfx_state_t *state, int num, uint32_t seg)
{
    ARG_CHECK(state, num < 16, GW_INVALID_SEGMENT_NUM);
    chk_Range(state, seg, sizeof(uint32_t));

    // Check that segment 0 is assigned to 0. In EX3 it's explicitly reserved
    // while in other ucode versions it's just conventional, so we treat it as
    // an error in all ucode versions.
    ARG_CHECK(state, num != 0 || seg == 0, GW_SEGZERO_NONZERO);

    state->segment_table[num] = seg & ~KSEG_MASK;
    state->segment_set_bits |= (1 << num);
    return 0;
}

static int
chk_SPSegment (gfx_state_t *state)
{
    int num      = gfxd_arg_value(0)->u;
    uint32_t seg = gfxd_arg_value(1)->u;

    return chk_Segment(state, num, seg);
}

static int
chk_SPRelSegment (gfx_state_t *state)
{
    int num         = gfxd_arg_value(0)->u;
    uint32_t relseg = gfxd_arg_value(1)->u;

    unsigned relnum = (relseg << 4) >> 28;
    uint8_t relnum8 = relseg >> 24;
    uint32_t reloff = relseg & 0xFFFFFF;

    if (relnum8 != 0x80 && relnum8 != 0xA0) // allow direct-mapped KSEG addresses
        ARG_CHECK(state, relnum8 < 16, GW_INVALID_SEGMENT_NUM_REL);

    ARG_CHECK(state, state->segment_set_bits & (1 << relnum), GW_UNSET_SEGMENT, relnum);

    uint32_t seg = state->segment_table[relnum] + reloff;
    return chk_Segment(state, num, seg);
}

static int
chk_SPMemset (gfx_state_t *state)
{
    uint32_t addr  = gfxd_arg_value(0)->u;
    uint16_t value = gfxd_arg_value(1)->u;
    uint32_t size  = gfxd_arg_value(2)->u;

    // Value must be 16-bit
    // Size must be a multiple of 16 and 24-bit

    // Address can be segmented
    uint32_t addr_phys = segmented_to_physical(state, addr);

    // Check DRAM range valid
    chk_Range(state, addr_phys, size);

    return 0;
}

void
print_mtx_params (FILE *print_out, uint32_t params)
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

    for (size_t i = 0; i < ARRAY_COUNT(mtx_params); i++)
    {
        if (!first)
            fprintf(print_out, " | ");

        fprintf(print_out, "%s", (mtx_params[i].value & params) ? mtx_params[i].set_name : mtx_params[i].unset_name);

        params &= ~mtx_params[i].value;
        first = false;
    }
    if (params != 0)
    {
        if (!first)
            fprintf(print_out, " | ");
        fprintf(print_out, "0x%08X", params);
    }
}

static void
print_mtx (MtxF *mf)
{
    gfxd_printf("        "  "/ %15.6f  %15.6f  %15.6f  %15.6f \\" "\n",
                mf->mf[0][0], mf->mf[0][1], mf->mf[0][2], mf->mf[0][3]);
    gfxd_printf("        "  "| %15.6f  %15.6f  %15.6f  %15.6f |"  "\n",
                mf->mf[1][0], mf->mf[1][1], mf->mf[1][2], mf->mf[1][3]);
    gfxd_printf("        "  "| %15.6f  %15.6f  %15.6f  %15.6f |"  "\n",
                mf->mf[2][0], mf->mf[2][1], mf->mf[2][2], mf->mf[2][3]);
    gfxd_printf("        " "\\ %15.6f  %15.6f  %15.6f  %15.6f /"  "\n",
                mf->mf[3][0], mf->mf[3][1], mf->mf[3][2], mf->mf[3][3]);
    gfxd_printf("\n");
}

static int
chk_SPMatrix (gfx_state_t *state)
{
    uint32_t matrix = gfxd_arg_value(0)->u;
    int param       = gfxd_arg_value(1)->i;

    uint32_t matrix_phys = segmented_to_physical(state, matrix);

    chk_Range(state, matrix_phys, sizeof(Mtx));

    int projection = (param & G_MTX_PROJECTION) == G_MTX_PROJECTION;
    int push = (param & G_MTX_PUSH) == G_MTX_PUSH;
    int load = (param & G_MTX_LOAD) == G_MTX_LOAD;

    ARG_CHECK(state, !(push && projection), GW_MTX_PUSHED_TO_PROJECTION);

    if (push)
    {
        ARG_CHECK(state, state->matrix_stack_depth * sizeof(Mtx) <= state->sp_dram_stack_size, GW_MTX_STACK_OVERFLOW);
        state->matrix_stack_depth++;
    }

    if (load)
    {
        if (projection)
            state->matrix_projection_set = true;
        else
            state->matrix_modelview_set = true;
    }
    else
    {
        if (projection)
        {
            ARG_CHECK(state, state->matrix_projection_set, GW_MUL_PROJECTION_UNSET);
        }
        else
        {
            ARG_CHECK(state, state->matrix_modelview_set, GW_MUL_MODELVIEW_UNSET);
        }
    }

    MtxF mf;
    Mtx mtx;

    if (!state->rdram->read_at(&mtx, matrix_phys, sizeof(Mtx)))
        goto err;

    mtx_to_mtxf(&mf, &mtx, true);

    if (state->options->print_matrices)
        print_mtx(&mf);

    if (projection)
    {
        if (!load)
        {
            mtxf_mtxf_mul_inplace(&state->projection_mtx, &mf);
        }
        else
        {
            state->projection_mtx = mf;
        }
    }
    else
    {
        if (!load)
        {
            mtxf_mtxf_mul_inplace(&mf, (MtxF*)obstack_peek(&state->mtx_stack));
        }

        if (push)
        {
            obstack_push(&state->mtx_stack, &mf);
        }
        else
        {
            *(MtxF*)obstack_peek(&state->mtx_stack) = mf;
        }
    }

    if (state->matrix_projection_set && state->matrix_modelview_set)
    {
        mtxf_mtxf_mul(&state->mvp_mtx, (MtxF*)obstack_peek(&state->mtx_stack), &state->projection_mtx);

        if (state->options->print_matrices)
            print_mtx(&state->mvp_mtx);
    }

    return 0;
err:
    gfxd_printf(VT_COL(RED,WHITE) "READ ERROR" VT_RST "\n");
    return 0;
}

static int
chk_scissor_cimg (gfx_state_t *state)
{
    // TODO it would be nice if we could check new cimg-scissor combinations as well as just the first two
    if (state->cimg_scissor_valid)
        return 0;
    state->cimg_scissor_valid = true;

    uint32_t width = state->last_cimg.width;

    ARG_CHECK(state, state->scissor.lrx <= state->scissor.ulx + (width << 2), GW_SCISSOR_TOO_WIDE);

    uint32_t idx;

    idx = state->scissor.ulx;
    if (state->scissor.uly > 0) {
        idx += width * (state->scissor.uly - qu102(1));
    }
    idx = (idx + 3) >> 2; // round up fractional values

    uint32_t scis_start_addr = state->last_cimg.addr + (G_SIZ_BITS(state->last_cimg.siz) * idx) / 8;

    idx = state->scissor.lrx;
    if (state->scissor.lry > 0) {
        idx += width * (state->scissor.lry - qu102(1));
    }
    idx = (idx + 3) >> 2; // round up fractional values

    uint32_t scis_end_addr   = state->last_cimg.addr + (G_SIZ_BITS(state->last_cimg.siz) * idx) / 8;

    // printf("\n(%08X,%u) + (%u,%u),(%u,%u) -> %08X,%08X\n", state->last_cimg.addr, state->last_cimg.width,
    //        state->scissor.ulx, state->scissor.uly, state->scissor.lrx, state->scissor.lry,
    //        scis_start_addr, scis_end_addr);

    ARG_CHECK(state, state->rdram->addr_valid(scis_start_addr), GW_SCISSOR_START_INVALID);
    ARG_CHECK(state, state->rdram->addr_valid(scis_end_addr-1), GW_SCISSOR_END_INVALID);
    return 0;
}

static int
chk_DPSetColorImage (gfx_state_t *state)
{
    int fmt = gfxd_arg_value(0)->i;
    int siz = gfxd_arg_value(1)->i;
    int width = gfxd_arg_value(2)->i;
    uint32_t cimg = gfxd_arg_value(3)->u;
    uint32_t cimg_phys = segmented_to_physical(state, cimg);

    CHECK_PIPESYNC(state);

    ARG_CHECK(state, chk_ValidImgFmt(fmt), GW_INVALID_CIMG_FMT);

    // We don't know the height of the color image, so can't check range
    chk_Range(state, cimg_phys, 0);

    ARG_CHECK(state, cimg_phys % 64 == 0, GW_BAD_CIMG_ALIGNMENT);

    switch (FMT_SIZ(fmt, siz))
    {
        case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_32b):
        case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_16b):
        case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_8b):
            break;
        default:
            ARG_CHECK(state, 0, GW_INVALID_CIMG_FMTSIZ);
            break;
    }

    state->last_cimg.fmt = fmt;
    state->last_cimg.siz = siz;
    state->last_cimg.width = width;
    state->last_cimg.addr = cimg_phys;
    state->cimg_set = true;
    
    if (state->scissor_set)
        return chk_scissor_cimg(state);
    return 0;
}

static int
chk_DPSetDepthImage (gfx_state_t *state)
{
    uint32_t zimg = gfxd_arg_value(0)->u;
    uint32_t zimg_phys = segmented_to_physical(state, zimg);

    CHECK_PIPESYNC(state);

    // depth image inherits its dimensions from the color image, however we cannot determine
    // the height of the color image therefore the height of the depth image is also indeterminate
    chk_Range(state, zimg_phys, 0);

    ARG_CHECK(state, zimg_phys % 64 == 0, GW_BAD_ZIMG_ALIGNMENT);

    state->last_zimg.addr = zimg_phys;
    state->zimg_set = true;
    return 0;
}

static int
chk_DPSetTextureImage (gfx_state_t *state)
{
    int fmt = gfxd_arg_value(0)->i;
    int siz = gfxd_arg_value(1)->i;
    int width = gfxd_arg_value(2)->i;
    uint32_t timg = gfxd_arg_value(3)->u;
    uint32_t timg_phys = segmented_to_physical(state, timg);

    // This attribute does not need a pipe sync, it is only used by load commands.
    // (it also does not need a load sync)

    ARG_CHECK(state, chk_ValidImgFmt(fmt), GW_INVALID_TIMG_FMT);
    ARG_CHECK(state, chk_ValidImgFmtSiz(fmt, siz), GW_INVALID_TIMG_FMTSIZ);

    chk_Range(state, timg_phys, 0);

    ARG_CHECK(state, (timg_phys % 8) == 0, GW_DANGEROUS_TEXTURE_ALIGNMENT);

    // EX3 material culling
    if (state->next_ucode == gfxd_f3dex3)
    {
        if (state->last_timg.addr != timg_phys) {
            state->ex3_mat_cull_mode = 1;
        } else {
            Note(gfxd_vprintf, "Texture image is the same as the previous, may cull loading");
            state->ex3_mat_cull_mode = -3;
        }
    }

    state->last_timg.fmt = fmt;
    state->last_timg.siz = siz;
    state->last_timg.width = width;
    state->last_timg.addr = timg_phys;
    return 0;
}

typedef struct
{
    float x,y,z;
} Vec3f;

void
mtxf_mulvec3 (Vec3f *dst, float *w, Vec3f *src, MtxF *mf) {
    dst->x = src->x * mf->mf[0][0] + src->y * mf->mf[1][0] + src->z * mf->mf[2][0] + mf->mf[3][0];
    dst->y = src->x * mf->mf[0][1] + src->y * mf->mf[1][1] + src->z * mf->mf[2][1] + mf->mf[3][1];
    dst->z = src->x * mf->mf[0][2] + src->y * mf->mf[1][2] + src->z * mf->mf[2][2] + mf->mf[3][2];
    *w     = src->x * mf->mf[0][3] + src->y * mf->mf[1][3] + src->z * mf->mf[2][3] + mf->mf[3][3];
}

static int
print_vtx (gfx_state_t *state, uint32_t vtx_addr, int v0, int num)
{
    state->rdram->seek(vtx_addr);

    for (int i = 0; i < num; i++)
    {
        if (v0 + num >= VTX_CACHE_SIZE)
        {
            // Don't process anything that's out of bounds
            break;
        }

        Vtx vtx;

        if (state->rdram->read(&vtx, sizeof(Vtx), 1) != 1)
            goto err;

        vtx.v.ob[0] = BSWAP16(vtx.v.ob[0]);
        vtx.v.ob[1] = BSWAP16(vtx.v.ob[1]);
        vtx.v.ob[2] = BSWAP16(vtx.v.ob[2]);
        vtx.v.flag  = BSWAP16(vtx.v.flag);
        vtx.v.tc[0] = BSWAP16(vtx.v.tc[0]);
        vtx.v.tc[1] = BSWAP16(vtx.v.tc[1]);

        gfxd_printf("        { { { %6d, %6d, %6d }, %d, { %6d, %6d }, { %4d, %4d, %4d, %4d } } }\n",
                    vtx.v.ob[0], vtx.v.ob[1], vtx.v.ob[2],
                    vtx.v.flag,
                    vtx.v.tc[0], vtx.v.tc[1],
                    vtx.v.cn[0], vtx.v.cn[1], vtx.v.cn[2], vtx.v.cn[3]);

        Vec3f model_pos;
        Vec3f screen_pos;
        float w;

        model_pos.x = vtx.v.ob[0];
        model_pos.y = vtx.v.ob[1];
        model_pos.z = vtx.v.ob[2];
        mtxf_mulvec3(&screen_pos, &w, &model_pos, &state->mvp_mtx);

#if 0
        gfxd_printf("                SCREEN POS { %13.6f %13.6f %13.6f %13.6f }\n",
                    (state->cur_vp.vp.vtrans[0] / 4.0f) + (screen_pos.x / w) * (state->cur_vp.vp.vscale[0] / 4.0f),
                    (state->cur_vp.vp.vtrans[1] / 4.0f) + (screen_pos.y / w) * (state->cur_vp.vp.vscale[1] / 4.0f),
                    (state->cur_vp.vp.vtrans[2] / 4.0f) + (screen_pos.z / w) * (state->cur_vp.vp.vscale[2] / 4.0f),
                    w);
#endif

        state->vtx_depths[v0 + i] = (screen_pos.z / w) * 1023.0f;
        state->vtx_w[v0 + i] = w;

        state->vtx_clipcodes[v0 + i] = 0;

        if (screen_pos.x > +w)
            state->vtx_clipcodes[v0 + i] |= CLIP_POSX;
        if (screen_pos.x < -w)
            state->vtx_clipcodes[v0 + i] |= CLIP_NEGX;
        if (screen_pos.y > +w)
            state->vtx_clipcodes[v0 + i] |= CLIP_POSY;
        if (screen_pos.y < -w)
            state->vtx_clipcodes[v0 + i] |= CLIP_NEGY;
        if (w < 0.01f)
            state->vtx_clipcodes[v0 + i] |= CLIP_W;

#if 0
        gfxd_printf("                CLIPCODES { +x %d -x %d +y %d -y %d w %d }\n",
                (vtx_clipcodes[v0 + i] & CLIP_POSX) != 0,
                (vtx_clipcodes[v0 + i] & CLIP_NEGX) != 0,
                (vtx_clipcodes[v0 + i] & CLIP_POSY) != 0,
                (vtx_clipcodes[v0 + i] & CLIP_NEGY) != 0,
                (vtx_clipcodes[v0 + i] & CLIP_W) != 0
            );
#endif
    }
    return 0;
err:
    gfxd_printf(VT_COL(RED,WHITE) "READ ERROR" VT_RST "\n");
    return -1;
}

static int
chk_SPVertex (gfx_state_t *state)
{
    uint32_t v = gfxd_arg_value(0)->u;
    int n      = gfxd_arg_value(1)->i;
    int v0     = gfxd_arg_value(2)->i;

    uint32_t v_phys = segmented_to_physical(state, v);

    chk_Range(state, v_phys, n * sizeof(Vtx));

    ARG_CHECK(state, n != 0, GW_VTX_LOADING_ZERO);
    ARG_CHECK(state, n <= VTX_CACHE_SIZE, GW_VTX_LOADING_TOO_MANY);
    ARG_CHECK(state, v0 + n <= VTX_CACHE_SIZE, GW_VTX_CACHE_OVERFLOW, n, v0);

    // TODO G_LIGHTING validation

    if (state->options->print_vertices)
        print_vtx(state, v_phys, v0, n);

    state->last_loaded_vtx_num = n;
    return 0;
}

static int
chk_SPViewport (gfx_state_t *state)
{
    uint32_t v      = gfxd_arg_value(0)->u;
    uint32_t v_phys = segmented_to_physical(state, v);

    chk_Range(state, v_phys, sizeof(Vp));

    if (!state->rdram->read_at(&state->cur_vp, v_phys, sizeof(Vp)))
        goto err;

    state->cur_vp.vp.vscale[0] = BSWAP16(state->cur_vp.vp.vscale[0]);
    state->cur_vp.vp.vscale[1] = BSWAP16(state->cur_vp.vp.vscale[1]);
    state->cur_vp.vp.vscale[2] = BSWAP16(state->cur_vp.vp.vscale[2]);
    state->cur_vp.vp.vtrans[0] = BSWAP16(state->cur_vp.vp.vtrans[0]);
    state->cur_vp.vp.vtrans[1] = BSWAP16(state->cur_vp.vp.vtrans[1]);
    state->cur_vp.vp.vtrans[2] = BSWAP16(state->cur_vp.vp.vtrans[2]);

    gfxd_printf(
       "        VIEWPORT(\n"
       "            .vscale.x = qs142(%8.2f), .vtrans.x = qs142(%8.2f)\n"
       "            .vscale.y = qs142(%8.2f), .vtrans.y = qs142(%8.2f)\n"
       "            .vscale.z = qs142(%8.2f), .vtrans.z = qs142(%8.2f)\n"
       "        );\n",
        state->cur_vp.vp.vscale[0] / 4.0f, state->cur_vp.vp.vtrans[0] / 4.0f,
        state->cur_vp.vp.vscale[1] / 4.0f, state->cur_vp.vp.vtrans[1] / 4.0f,
        state->cur_vp.vp.vscale[2] / 4.0f, state->cur_vp.vp.vtrans[2] / 4.0f
    );

    return 0;
err:
    gfxd_printf(VT_COL(RED,WHITE) "READ ERROR" VT_RST "\n");
    return -1;
}

enum prim_type
{
    PRIM_TYPE_TRI,
    PRIM_TYPE_TEXRECT,
    PRIM_TYPE_FILLRECT
};

#define BL_CYC_IS_SET(bl, clk) \
    ((bl)->m1a_c##clk != 0 || (bl)->m1b_c##clk != 0 || (bl)->m2a_c##clk != 0 || (bl)->m2b_c##clk != 0)

#define CC_C_HAS(cc, input, clk) \
    ((cc)->a##clk == (input) || (cc)->b##clk == (input) || (cc)->c##clk == (input) || (cc)->d##clk == (input))

#define CC_A_HAS(cc, input, clk) \
    ((cc)->Aa##clk == (input) || (cc)->Ab##clk == (input) || (cc)->Ac##clk == (input) || (cc)->Ad##clk == (input))

static int
chk_render_tile (gfx_state_t *state, int tile)
{
    int cycle_type = OTHERMODE_VAL(state, hi, CYCLETYPE);
    int tlut_en = OTHERMODE_VAL(state, hi, TEXTLUT) != G_TT_NONE;

    tile_descriptor_t *tile_desc = get_tile_desc(state, tile);
    ARG_CHECK(state, tile_desc != NULL, GW_TILEDESC_BAD);

    if (tile_desc != NULL)
    {
        if (tile_desc->fmt == G_IM_FMT_CI)
            ARG_CHECK(state, tlut_en, GW_CI_RENDER_TILE_NO_TLUT);
        else
            ARG_CHECK(state, !tlut_en, GW_NO_CI_RENDER_TILE_TLUT);

        if (cycle_type == G_CYC_COPY)
        {
            if (state->last_cimg.siz != G_IM_SIZ_8b)
                ARG_CHECK(state, tile_desc->siz != G_IM_SIZ_4b && tile_desc->siz != G_IM_SIZ_8b,
                            GW_COPYMODE_MISMATCH_8B);

            if (state->last_cimg.siz == G_IM_SIZ_16b)
                ARG_CHECK(state, tile_desc->siz == G_IM_SIZ_16b, GW_COPYMODE_MISMATCH_16B);
        }
    }

    // TODO if triangle is textured, are loadsync and/or tilesync needed?
    state->tile_busy[tile] = 1;
    state->load_busy = true;
    return 0;
}

static int
chk_render_primitive (gfx_state_t *state, enum prim_type prim_type, int tile)
{
    bool uses_texel1 = false;

    uint32_t rm = OTHERMODE_VAL(state, lo, RENDERMODE);
    int cycle_type = OTHERMODE_VAL(state, hi, CYCLETYPE);

    ARG_CHECK(state, !((cycle_type == G_CYC_FILL && state->last_cimg.siz == G_IM_SIZ_4b)), GW_FILLMODE_4B);
    ARG_CHECK(state, !((cycle_type == G_CYC_COPY && state->last_cimg.siz == G_IM_SIZ_32b)), GW_COPYMODE_32B);

    ARG_CHECK(state, state->scissor_set, GW_SCISSOR_UNSET);
    ARG_CHECK(state, state->cimg_set, GW_CIMG_UNSET);

    bool bl_c1_set = BL_CYC_IS_SET(&state->bl, 1);
    bool bl_c2_set = BL_CYC_IS_SET(&state->bl, 2);
    bool blend_en = (rm & (AA_EN | FORCE_BL)) != 0;

    ARG_CHECK(state, blend_en || !(bl_c1_set || bl_c2_set), GW_BLENDER_SET_BUT_UNUSED);

    if (cycle_type == G_CYC_FILL)
    {
        ARG_CHECK(state, prim_type != PRIM_TYPE_TRI, GW_TRI_IN_FILLMODE);
        ARG_CHECK(state, prim_type != PRIM_TYPE_TEXRECT, GW_TEXRECT_IN_FILLMODE);

        if (prim_type == PRIM_TYPE_FILLRECT)
            ARG_CHECK(state, state->fill_color_set, GW_FILLRECT_FILLCOLOR_UNSET);
    }

    if (cycle_type != G_CYC_FILL && cycle_type != G_CYC_COPY)
    {
        // Check SHADE inputs
        // Fillrect doesn't seem to care about any of this
        if (prim_type != PRIM_TYPE_FILLRECT)
        {
            // If G_SHADE is unset shade coefficients are not generated for RDP triangles, so shade inputs to CC and BL
            // are prohibited as they will read garbage
            if (prim_type == PRIM_TYPE_TEXRECT || !(state->geometry_mode & G_SHADE))
            {
                const char *errmsg = (prim_type == PRIM_TYPE_TEXRECT)
                                   ? "rendering textured rectangle"
                                   : "G_SHADE not set in geometry mode";

                ARG_CHECK(state, !CC_C_HAS(&state->cc, G_CCMUX_SHADE, 0) && !(state->cc.c0 == G_CCMUX_SHADE_ALPHA),
                          GW_CC_SHADE_INVALID, 1, "RGB", errmsg);
                ARG_CHECK(state, !CC_A_HAS(&state->cc, G_ACMUX_SHADE, 0),
                          GW_CC_SHADE_INVALID, 1, "Alpha", errmsg);
                ARG_CHECK(state, !CC_C_HAS(&state->cc, G_CCMUX_SHADE, 1) && !(state->cc.c1 == G_CCMUX_SHADE_ALPHA),
                          GW_CC_SHADE_INVALID, 2, "RGB", errmsg);
                ARG_CHECK(state, !CC_A_HAS(&state->cc, G_ACMUX_SHADE, 1),
                          GW_CC_SHADE_INVALID, 2, "Alpha", errmsg);

                ARG_CHECK(state, !(state->bl.m1b_c1 == G_BL_A_SHADE), GW_CC_SHADE_ALPHA_INVALID, 1, errmsg);
                ARG_CHECK(state, !(state->bl.m1b_c2 == G_BL_A_SHADE), GW_CC_SHADE_ALPHA_INVALID, 2, errmsg);
            }
        }
    }

    uint32_t zsrc = OTHERMODE_VAL(state, lo, ZSRCSEL);
    int z_cmp = (rm & Z_CMP) != 0;
    int z_upd = (rm & Z_UPD) != 0;

    // Check z-buffer settings if enabled
    if (z_cmp || z_upd)
    {
        if (prim_type == PRIM_TYPE_TRI)
        {
            ARG_CHECK(state, zsrc == G_ZS_PRIM || (state->geometry_mode & G_ZBUFFER),
                      GW_ZS_PIXEL_SET_WITHOUT_G_ZBUFFER);
        }
        else
        {
            ARG_CHECK(state, zsrc == G_ZS_PRIM, GW_ZSRC_INVALID);
        }
    }

    switch (cycle_type)
    {
        case G_CYC_1CYCLE:
            // check blender configuration is the same for 1-Cycle mode
            // TODO check this
            ARG_CHECK(state, state->bl.m1a_c1 == state->bl.m1a_c2 && state->bl.m1b_c1 == state->bl.m1b_c2 &&
                             state->bl.m2a_c1 == state->bl.m2a_c2 && state->bl.m2b_c1 == state->bl.m2b_c2,
                             GW_BLENDER_STAGES_DIFFER_1CYC);

            // check cc configuration is the same for 1-Cycle mode
            ARG_CHECK(state, state->cc.a0 == state->cc.a1 && state->cc.b0 == state->cc.b1 &&
                             state->cc.c0 == state->cc.c1 && state->cc.d0 == state->cc.d1 &&
                             state->cc.Aa0 == state->cc.Aa1 && state->cc.Ab0 == state->cc.Ab1 &&
                             state->cc.Ac0 == state->cc.Ac1 && state->cc.Ad0 == state->cc.Ad1,
                             GW_CC_STAGES_DIFFER_1CYC);

            // check cc does not use COMBINED input
            ARG_CHECK(state, !CC_C_HAS(&state->cc, G_CCMUX_COMBINED, 1), GW_CC_COMBINED_IN_C1, "RGB");
            ARG_CHECK(state, !CC_A_HAS(&state->cc, G_ACMUX_COMBINED, 1), GW_CC_COMBINED_IN_C1, "Alpha");

            // check cc doe not use COMBINED_ALPHA input
            ARG_CHECK(state, state->cc.c1 != G_CCMUX_COMBINED_ALPHA, GW_CC_COMBINED_ALPHA_IN_C1);

            // check cc does not use TEXEL1 input
            ARG_CHECK(state, !CC_C_HAS(&state->cc, G_CCMUX_TEXEL1, 1), GW_CC_TEXEL1_RGB_1CYC);
            ARG_CHECK(state, !CC_A_HAS(&state->cc, G_ACMUX_TEXEL1, 1), GW_CC_TEXEL1_ALPHA_1CYC);

            // check cc doe not use TEXEL1_ALPHA input
            ARG_CHECK(state, state->cc.c1 != G_CCMUX_TEXEL1_ALPHA, GW_CC_TEXEL1_RGBA_1CYC);
            break;
        case G_CYC_2CYCLE:
            // check cc does not use COMBINED input in the first cycle
            ARG_CHECK(state, !CC_C_HAS(&state->cc, G_CCMUX_COMBINED, 0), GW_CC_COMBINED_IN_C2_C1, "RGB");
            ARG_CHECK(state, !CC_A_HAS(&state->cc, G_ACMUX_COMBINED, 0), GW_CC_COMBINED_IN_C2_C1, "Alpha");

            // check cc doe not use COMBINED_ALPHA input in the first cycle
            ARG_CHECK(state, state->cc.c0 != G_CCMUX_COMBINED_ALPHA, GW_CC_COMBINED_ALPHA_IN_C2_C1);

            // check cc does not use TEXEL1 input in the second cycle
            ARG_CHECK(state, !CC_C_HAS(&state->cc, G_CCMUX_TEXEL1, 1), GW_CC_TEXEL1_RGB_C2_2CYC);
            ARG_CHECK(state, !CC_A_HAS(&state->cc, G_ACMUX_TEXEL1, 1), GW_CC_TEXEL1_ALPHA_C2_2CYC);

            // check cc doe not use TEXEL1_ALPHA input in the second cycle
            ARG_CHECK(state, state->cc.c1 != G_CCMUX_TEXEL1_ALPHA, GW_CC_TEXEL1_RGBA_C2_2CYC);

            uses_texel1 = (prim_type != PRIM_TYPE_FILLRECT) &&
                          (CC_C_HAS(&state->cc, G_CCMUX_TEXEL1, 0) || CC_C_HAS(&state->cc, G_CCMUX_TEXEL0, 1) ||
                           CC_A_HAS(&state->cc, G_ACMUX_TEXEL1, 0) || CC_A_HAS(&state->cc, G_ACMUX_TEXEL0, 1));
            break;
        case G_CYC_FILL:
            ARG_CHECK(state, !((rm & IM_RD) || z_cmp), GW_FILLMODE_CIMG_ZIMG_RD_PER_PIXEL);
            ARG_CHECK(state, !(z_upd && zsrc == G_ZS_PIXEL), GW_FILLMODE_ZIMG_WR_PER_PIXEL);
            break;
        case G_CYC_COPY:;
            ARG_CHECK(state, !((rm & IM_RD) || z_cmp), GW_COPYMODE_CIMG_ZIMG_RD_PER_PIXEL);
            ARG_CHECK(state, !(z_upd && zsrc == G_ZS_PIXEL), GW_COPYMODE_ZIMG_WR_PER_PIXEL);

            ARG_CHECK(state, !(rm & AA_EN), GW_COPYMODE_AA);
            ARG_CHECK(state, rm == 0, GW_COPYMODE_BL_SET);
            ARG_CHECK(state, OTHERMODE_VAL(state, hi, TEXTFILT) == G_TF_POINT, GW_COPYMODE_TEXTURE_FILTER);
            break;
    }

    if (prim_type == PRIM_TYPE_TEXRECT || (prim_type == PRIM_TYPE_TRI && state->render_tile_on))
    {
        chk_render_tile(state, tile);
        if (uses_texel1)
        {
            if (tile == 7) // TODO warning? don't bother?
                Note(gfxd_vprintf, "TEXEL0 was tile 7 so TEXEL1 is sourced from tile 0");
            chk_render_tile(state, (tile + 1) & 7);
        }
    }
    state->pipe_busy = true;
    return 0;
}

static int
chk_DPFillTriangle (gfx_state_t *state, int tnum, int v0, int v1, int v2, int flag)
{
    int last_loaded_vtx_num = state->last_loaded_vtx_num;

    // TODO this is often OK and even wanted for optimized rendering, this should be changed to use a stack-based
    // approach i.e. if a DL loads vertices and renders with them before returning and then those verts are used again
    // without a reload, that should be flagged.
    ARG_CHECK(state, v0 < last_loaded_vtx_num && v1 < last_loaded_vtx_num && v2 < last_loaded_vtx_num,
              GW_TRI_LEECHING_VERTS, tnum);

    ARG_CHECK(state, v0 < VTX_CACHE_SIZE && v1 < VTX_CACHE_SIZE && v2 < VTX_CACHE_SIZE, GW_TRI_VTX_OOB, tnum);

    if (state->render_tile_on)
    {
        // This is only a warning as a textured triangle will pass an inverse W either way, so no garbage gets read
        ARG_CHECK(state, OTHERMODE_VAL(state, hi, TEXTPERSP) == G_TP_PERSP, GW_TRI_TXTR_NOPERSP);
    }

    // TODO clip triangles

    // Base triangle size
    size_t rdp_tri_size = 0x20;
    // Additional optional data
    if (state->geometry_mode & G_SHADE)
        rdp_tri_size += 0x40;
    if (state->render_tile_on)
        rdp_tri_size += 0x40;
    if (state->geometry_mode & G_ZBUFFER)
        rdp_tri_size += 0x20;

    return chk_render_primitive(state, PRIM_TYPE_TRI, state->render_tile);
}

static int
chk_SP1Quadrangle (gfx_state_t *state)
{
    int v0 = gfxd_arg_value(0)->i;
    int v1 = gfxd_arg_value(1)->i;
    int v2 = gfxd_arg_value(2)->i;
    int v3 = gfxd_arg_value(3)->i;
    int flag = gfxd_arg_value(4)->i;

    chk_DPFillTriangle(state, 1, v0, v1, v2, flag);
    chk_DPFillTriangle(state, 2, v0, v2, v3, flag);
    return 0;
}

static int
chk_SP2Triangles (gfx_state_t *state)
{
    int v00 = gfxd_arg_value(0)->i;
    int v01 = gfxd_arg_value(1)->i;
    int v02 = gfxd_arg_value(2)->i;
    int flag0 = gfxd_arg_value(3)->i;

    int v10 = gfxd_arg_value(4)->i;
    int v11 = gfxd_arg_value(5)->i;
    int v12 = gfxd_arg_value(6)->i;
    int flag1 = gfxd_arg_value(7)->i;

    chk_DPFillTriangle(state, 1, v00, v01, v02, flag0);
    chk_DPFillTriangle(state, 2, v10, v11, v12, flag1);
    return 0;
}

static int
chk_SP1Triangle (gfx_state_t *state)
{
    int v0 = gfxd_arg_value(0)->i;
    int v1 = gfxd_arg_value(1)->i;
    int v2 = gfxd_arg_value(2)->i;
    int flag = gfxd_arg_value(3)->i;

    chk_DPFillTriangle(state, 1, v0, v1, v2, flag);
    return 0;
}

static int
chk_SPLine3D (gfx_state_t *state)
{
    int v0 = gfxd_arg_value(0)->i;
    int v1 = gfxd_arg_value(1)->i;
    int flag = gfxd_arg_value(2)->i;

    chk_DPFillTriangle(state, 1, v0, v0, v1, flag);
    return 0;
}

static int
chk_SPLineW3D (gfx_state_t *state)
{
    int v0 = gfxd_arg_value(0)->i;
    int v1 = gfxd_arg_value(1)->i;
    int wd = gfxd_arg_value(2)->i;
    int flag = gfxd_arg_value(3)->i;

    chk_DPFillTriangle(state, 1, v0, v0, v1, flag);
    return 0;
}

static int
chk_DPFillRectangle (gfx_state_t *state)
{
    qu102_t ulx = gfxd_arg_value(0)->u;
    qu102_t uly = gfxd_arg_value(1)->u;
    qu102_t lrx = gfxd_arg_value(2)->u;
    qu102_t lry = gfxd_arg_value(3)->u;

    // TODO

    return chk_render_primitive(state, PRIM_TYPE_FILLRECT, -1);
}

static int
chk_DPSetCombine (gfx_state_t *state)
{
    uint32_t *cc_data = (uint32_t *)(gfxd_macro_data() + gfxd_macro_offset());
    uint32_t cc_hi = BSWAP32(cc_data[0]);
    uint32_t cc_lo = BSWAP32(cc_data[1]);

    CHECK_PIPESYNC(state);

    state->combiner_hi = cc_hi;
    state->combiner_lo = cc_lo;
    cc_decode(&state->cc, state->combiner_hi, state->combiner_lo);

    return 0;
}

static int
chk_geometrymode (gfx_state_t *state, uint32_t clear, uint32_t set)
{
    state->geometry_mode &= ~clear;
    state->geometry_mode |= set;

    // TODO check for G_LIGHTING set without G_SHADE and warn
    // TODO check for texgen without G_LIGHTING and warn
    return 0;
}

static int
chk_SPGeometryMode (gfx_state_t *state)
{
    uint32_t clear = gfxd_arg_value(0)->u;
    uint32_t set   = gfxd_arg_value(1)->u;

    return chk_geometrymode(state, clear, set);
}

static int
chk_SPLoadGeometryMode (gfx_state_t *state)
{
    uint32_t clear = 0xFFFFFFFF;
    uint32_t set   = gfxd_arg_value(0)->u;

    return chk_geometrymode(state, clear, set);
}

static int
chk_SPSetGeometryMode (gfx_state_t *state)
{
    uint32_t clear = 0;
    uint32_t set   = gfxd_arg_value(0)->u;

    return chk_geometrymode(state, clear, set);
}

static int
chk_SPClearGeometryMode (gfx_state_t *state)
{
    uint32_t clear = gfxd_arg_value(0)->u;
    uint32_t set   = 0;

    return chk_geometrymode(state, clear, set);
}

static int
chk_othermode (gfx_state_t *state)
{
    CHECK_PIPESYNC(state);
    bl_decode(&state->bl, OTHERMODE_VAL(state, lo, RENDERMODE));
    return 0;
}

static int
chk_DPPipelineMode (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(PIPELINE);
    state->othermode_hi |= mode & MDMASK(PIPELINE);
    return chk_othermode(state);
}

static int
chk_DPSetAlphaDither (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(ALPHADITHER);
    state->othermode_hi |= mode & MDMASK(ALPHADITHER);
    return chk_othermode(state);
}

static int
chk_DPSetColorDither (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(RGBDITHER);
    state->othermode_hi |= mode & MDMASK(RGBDITHER);
    return chk_othermode(state);
}

static int
chk_DPSetTextureConvert (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(TEXTCONV);
    state->othermode_hi |= mode & MDMASK(TEXTCONV);
    return chk_othermode(state);
}

static int
chk_DPSetCycleType (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(CYCLETYPE);
    state->othermode_hi |= mode & MDMASK(CYCLETYPE);
    return chk_othermode(state);
}

static int
chk_DPSetCombineKey (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(COMBKEY);
    state->othermode_hi |= mode & MDMASK(COMBKEY);
    return chk_othermode(state);
}

static int
chk_DPSetTextureDetail (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(TEXTDETAIL);
    state->othermode_hi |= mode & MDMASK(TEXTDETAIL);
    return chk_othermode(state);
}

static int
chk_DPSetTextureFilter (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(TEXTFILT);
    state->othermode_hi |= mode & MDMASK(TEXTFILT);
    return chk_othermode(state);
}

static int
chk_DPSetTextureLOD (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(TEXTLOD);
    state->othermode_hi |= mode & MDMASK(TEXTLOD);
    return chk_othermode(state);
}

static int
chk_DPSetTextureLUT (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(TEXTLUT);
    state->othermode_hi |= mode & MDMASK(TEXTLUT);
    return chk_othermode(state);
}

static int
chk_DPSetTexturePersp (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_hi &= (~0) & ~MDMASK(TEXTPERSP);
    state->othermode_hi |= mode & MDMASK(TEXTPERSP);
    return chk_othermode(state);
}

static int
chk_DPSetAlphaCompare (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_lo &= (~0) & ~MDMASK(ALPHACOMPARE);
    state->othermode_lo |= mode & MDMASK(ALPHACOMPARE);
    return chk_othermode(state);
}

static int
chk_DPSetDepthSource (gfx_state_t *state)
{
    unsigned mode = gfxd_arg_value(0)->u;

    state->othermode_lo &= (~0) & ~MDMASK(ZSRCSEL);
    state->othermode_lo |= mode & MDMASK(ZSRCSEL);
    return chk_othermode(state);
}

static int
chk_DPSetRenderMode (gfx_state_t *state)
{
    unsigned mode1 = gfxd_arg_value(0)->u;
    unsigned mode2 = gfxd_arg_value(1)->u;

    state->othermode_lo &= (~0) & ~MDMASK(RENDERMODE);
    state->othermode_lo |= mode1 & MDMASK(RENDERMODE);
    state->othermode_lo |= mode2 & MDMASK(RENDERMODE);

    return chk_othermode(state);
}

static int
chk_SPSetOtherModeHi (gfx_state_t *state)
{
    int sft = gfxd_arg_value(0)->i;
    int len = gfxd_arg_value(1)->i;
    unsigned mode = gfxd_arg_value(2)->u;
    unsigned mask = ((1 << len) - 1) << sft;

    state->othermode_hi &= (~0) & ~mask;
    state->othermode_hi |= mode & mask;
    return chk_othermode(state);
}

static int
chk_SPSetOtherModeLo (gfx_state_t *state)
{
    int sft = gfxd_arg_value(0)->i;
    int len = gfxd_arg_value(1)->i;
    unsigned mode = gfxd_arg_value(2)->u;
    unsigned mask = ((1 << len) - 1) << sft;

    state->othermode_lo &= (~0) & ~mask;
    state->othermode_lo |= mode & mask;
    return chk_othermode(state);
}

static int
chk_SPSetOtherMode (gfx_state_t *state)
{
    int opc = gfxd_arg_value(0)->i;
    int sft = gfxd_arg_value(1)->i;
    int len = gfxd_arg_value(2)->i;
    unsigned mode = gfxd_arg_value(3)->u;
    unsigned mask = ((1 << len) - 1) << sft;

    switch (opc)
    {
        case G_SETOTHERMODE_H:
            state->othermode_hi &= (~0) & ~mask;
            state->othermode_hi |= mode & mask;
            break;
        case G_SETOTHERMODE_L:
            state->othermode_lo &= (~0) & ~mask;
            state->othermode_lo |= mode & mask;
            break;
        default:
            assert(!"bad opcode in SPSetOtherMode?");
    }
    return chk_othermode(state);
}

static int
chk_DPSetOtherMode (gfx_state_t *state)
{
    uint32_t hi = gfxd_arg_value(0)->u;
    uint32_t lo = gfxd_arg_value(1)->u;

    state->othermode_hi = hi;
    state->othermode_lo = lo;
    return chk_othermode(state);
}

#define TMEM_SIZE 0x1000
#define LOADBLOCK_MAX_TEXELS 2048 // trying to load more than this amount of texels loads nothing

static int
chk_DPLoadBlock (gfx_state_t *state)
{
    int tile = gfxd_arg_value(0)->i;
    unsigned uls = gfxd_arg_value(1)->u;
    unsigned ult = gfxd_arg_value(2)->u;
    unsigned lrs = gfxd_arg_value(3)->u;
    unsigned dxt = gfxd_arg_value(4)->u;

    tile_descriptor_t *tile_desc = get_tile_desc(state, tile);

    CHECK_LOADSYNC(state);

    uint32_t timg_phys_low = state->last_timg.addr & 0b111111;
    uint32_t line_texels = lrs - uls + 1;
    ARG_CHECK(state, timg_phys_low == 0 || timg_phys_low >= 8 || state->last_timg.siz == G_IM_SIZ_4b
              || (line_texels * G_SIZ_BITS(state->last_timg.siz) < 58 * 8), GW_BAD_TIMG_ALIGNMENT);

    ARG_CHECK(state, line_texels <= LOADBLOCK_MAX_TEXELS, GW_LOADBLOCK_TOO_MANY_TEXELS);

    // TODO more checks

    state->pipe_busy = true;
    // load_busy = true;

    ARG_CHECK(state, state->last_timg.siz != G_IM_SIZ_4b, GW_TIMG_LOAD_4B);
    ARG_CHECK(state, tile_desc != NULL, GW_TILEDESC_BAD);
    if (tile_desc != NULL)
    {

        ARG_CHECK(state, tile_desc->fmt == state->last_timg.fmt && tile_desc->siz == state->last_timg.siz,
                  GW_TIMG_TILE_LOAD_NONMATCHING);

        tile_desc->uls = uls; // sl
        tile_desc->ult = ult; // tl
        tile_desc->lrs = lrs; // sh
        tile_desc->lrt = dxt; // th

        // TODO loading emulation
    }
    return 0;
}

static int
chk_DPLoadTile (gfx_state_t *state)
{
    int tile = gfxd_arg_value(0)->i;
    unsigned uls = gfxd_arg_value(1)->u;
    unsigned ult = gfxd_arg_value(2)->u;
    unsigned lrs = gfxd_arg_value(3)->u;
    unsigned lrt = gfxd_arg_value(4)->u;

    tile_descriptor_t *tile_desc = get_tile_desc(state, tile);

    CHECK_LOADSYNC(state);

    uint32_t timg_phys_low = state->last_timg.addr & 0b111111;
    uint32_t line_texels = lrs - uls + 1;
    ARG_CHECK(state, timg_phys_low == 0 || timg_phys_low >= 8 || state->last_timg.siz == G_IM_SIZ_4b
              || (line_texels * G_SIZ_BITS(state->last_timg.siz) / 8 < 58), GW_BAD_TIMG_ALIGNMENT);

    ARG_CHECK(state, state->last_timg.siz != G_IM_SIZ_4b, GW_TIMG_LOAD_4B);

    // TODO
    state->pipe_busy = true;
    // load_busy = true;

    ARG_CHECK(state, tile_desc != NULL, GW_TILEDESC_BAD);
    if (tile_desc != NULL)
    {
        tile_desc->uls = uls;
        tile_desc->ult = ult;
        tile_desc->lrs = lrs;
        tile_desc->lrt = lrt;
    }
    return 0;
}

static int
chk_DPLoadTLUTCmd (gfx_state_t *state)
{
    uint32_t *ltlut_data = (uint32_t *)gfxd_macro_data();
    uint32_t ltlut_hi = BSWAP32(ltlut_data[0]);
    uint32_t ltlut_lo = BSWAP32(ltlut_data[1]);

    // LoadTLUTCmd actually has more fields, it's the same command format as LoadTile
    // however the macros provided for this command by the SDK leave most as 0.
    int tile = SHIFTR(ltlut_lo, 3, 24);
    qu102_t uls = SHIFTR(ltlut_hi, 12, 12);
    qu102_t ult = SHIFTR(ltlut_hi, 12,  0);
    qu102_t lrs = SHIFTR(ltlut_lo, 12, 12);
    qu102_t lrt = SHIFTR(ltlut_lo, 12,  0);

    int count = lrs >> 2; // count here is actually count - 1
    tile_descriptor_t *tile_desc = get_tile_desc(state, tile);

    CHECK_LOADSYNC(state);

    // LoadTLUTCmd will crash if floor(lrt) > floor(ult)
    // where floor(x) is x & ~0b11 (removes 10.2 fractional part)
    ARG_CHECK(state, (lrt >> 2) <= (ult >> 2), GW_TLUT_BAD_COORDS);

    ARG_CHECK(state, count < 256, GW_TLUT_TOO_LARGE);

    uint32_t timg_phys_low = state->last_timg.addr & 0x3F;
    uint32_t line_texels = count + 1;
    ARG_CHECK(state, timg_phys_low == 0 || timg_phys_low >= 8 || state->last_timg.siz == G_IM_SIZ_4b
              || (line_texels * G_SIZ_BITS(state->last_timg.siz) / 8 < 58), GW_BAD_TIMG_ALIGNMENT);

    ARG_CHECK(state, state->last_timg.siz != G_IM_SIZ_4b, GW_TIMG_LOAD_4B);

    int timg_fmtsiz = FMT_SIZ(state->last_timg.fmt, state->last_timg.siz);
    ARG_CHECK(state, timg_fmtsiz == FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_16b)
              || timg_fmtsiz == FMT_SIZ(G_IM_FMT_IA, G_IM_SIZ_16b), GW_TLUT_BAD_FMT);

    state->pipe_busy = true;
    // load_busy = true;

    ARG_CHECK(state, tile_desc != NULL, GW_TILEDESC_BAD);
    if (tile_desc != NULL)
    {
        ARG_CHECK(state, tile_desc->tmem >= 0x100, GW_TLUT_BAD_TMEM_ADDR);

        tile_desc->uls = uls;
        tile_desc->ult = ult;
        tile_desc->lrs = lrs;
        tile_desc->lrt = lrt;
    }
    return 0;
}

static int
chk_DPPipeSync (gfx_state_t *state)
{
    ARG_CHECK(state, state->pipe_busy, GW_SUPERFLUOUS_PIPESYNC);
    state->pipe_busy = false;
    return 0;
}

static int
chk_DPLoadSync (gfx_state_t *state)
{
    ARG_CHECK(state, state->load_busy, GW_SUPERFLUOUS_LOADSYNC);
    state->load_busy = false;
    return 0;
}

static int
chk_DPTileSync (gfx_state_t *state)
{
    // tilesync will sync all tile descrptiors, it's hard to knowif a tilesync is superfluous
    // (doesn't help that it's not clear when a tilesync should even be done)

    // ARG_CHECK(state, state->tile_busy[], GW_SUPERFLUOUS_TILESYNC);

    memset(state->tile_busy, 0, sizeof(state->tile_busy));
    return 0;
}

static int
chk_DPSetEnvColor (gfx_state_t *state)
{
    CHECK_PIPESYNC(state);

    // TODO

    return 0;
}

static int
chk_DPSetFogColor (gfx_state_t *state)
{
    CHECK_PIPESYNC(state);

    // TODO

    return 0;
}

static int
chk_DPSetBlendColor (gfx_state_t *state)
{
    CHECK_PIPESYNC(state);

    // TODO

    return 0;
}

static int
chk_DPSetFillColor (gfx_state_t *state)
{
    uint32_t color = gfxd_arg_value(0)->u;

    CHECK_PIPESYNC(state);

    state->fill_color = color;
    state->fill_color_set = true;

    return 0;
}

static int
chk_DPSetPrimColor (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_DPSetTile (gfx_state_t *state)
{
    int fmt       = gfxd_arg_value(0)->i;
    int siz       = gfxd_arg_value(1)->i;
    int line      = gfxd_arg_value(2)->i;
    uint16_t tmem = gfxd_arg_value(3)->u;
    int tile      = gfxd_arg_value(4)->i;
    int pal       = gfxd_arg_value(5)->i;
    unsigned cmt  = gfxd_arg_value(6)->u;
    int maskt     = gfxd_arg_value(7)->i;
    int shiftt    = gfxd_arg_value(8)->i;
    unsigned cms  = gfxd_arg_value(9)->u;
    int masks     = gfxd_arg_value(10)->i;
    int shifts    = gfxd_arg_value(11)->i;

    tile_descriptor_t *tile_desc = get_tile_desc(state, tile);

    CHECK_TILESYNC(state, tile);

    ARG_CHECK(state, tile_desc != NULL, GW_TILEDESC_BAD);

    ARG_CHECK(state, chk_ValidImgFmt(fmt), GW_INVALID_TIMG_FMT);

    if (fmt == G_IM_FMT_CI && siz == G_IM_SIZ_8b)
        ARG_CHECK(state, pal == 0, GW_TEX_CI8_NONZERO_PAL);

    if (fmt == G_IM_FMT_YUV || (fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_32b))
        ARG_CHECK(state, tmem * 8 < 0x800, GW_TIMG_BAD_TMEM_ADDR, (fmt == G_IM_FMT_YUV) ? "YUV" : "RGBA32");

    // TODO more checks

    if (tile_desc != NULL)
    {
        tile_desc->fmt = fmt;
        tile_desc->siz = siz;
        tile_desc->line = line;
        tile_desc->tmem = tmem;
        tile_desc->palette = pal;
        tile_desc->cmt = cmt;
        tile_desc->maskt = maskt;
        tile_desc->shiftt = shiftt;
        tile_desc->cms = cms;
        tile_desc->masks = masks;
        tile_desc->shifts = shifts;
    }
    return 0;
}

static int
chk_Invalid (gfx_state_t *state)
{
    WARNING_ERROR(state, GW_INVALID_GFX_CMD);
    return 0;
}

static int
chk_DPFullSync (gfx_state_t *state)
{
    state->pipe_busy = false;
    memset(state->tile_busy, 0, sizeof(state->tile_busy));
    state->load_busy = false;
    state->fullsync = true;
    return 0;
}

static int
chk_DPSetConvert (gfx_state_t *state)
{
    CHECK_PIPESYNC(state);

    // TODO

    return 0;
}

static int
chk_DPSetKeyGB (gfx_state_t *state)
{
    CHECK_PIPESYNC(state);

    // TODO

    return 0;
}

static int
chk_DPSetKeyR (gfx_state_t *state)
{
    CHECK_PIPESYNC(state);

    // TODO

    return 0;
}

static uint8_t
dz_compress (int dz)
{
    // RDP-accurate integer log2, guaranteed correct only for powers of 2.

    int val = 0;
    if (dz & 0xFF00)
        val |= 8;
    if (dz & 0xF0F0)
        val |= 4;
    if (dz & 0xCCCC)
        val |= 2;
    if (dz & 0xAAAA)
        val |= 1;
    return val;
}

static uint8_t
integer_log2 (int x)
{
    uint8_t log = 0;
    while (x >>= 1)
        log++;
    return log;
}

static int
chk_DPSetPrimDepth (gfx_state_t *state)
{
    uint16_t z = (uint16_t)gfxd_arg_value(0)->i;
    uint16_t dz = (uint16_t)gfxd_arg_value(1)->i;

    // Compute the fast log2 of dz that hardware uses
    uint8_t dz_compressed_hw = dz_compress(dz);
    // Compute the accurate log2 of dz
    uint8_t dz_compressed_exact = integer_log2(dz);

    ARG_CHECK(state, dz_compressed_hw == dz_compressed_exact, GW_RDP_LOG2_INACCURATE);

    return 0;
}

static int
chk_scissor (gfx_state_t *state, int mode, qu102_t ulx, qu102_t uly, qu102_t lrx, qu102_t lry)
{
    state->scissor_set = true;

    // TODO scissor bounds change based on cycle type
    ARG_CHECK(state, lrx > ulx && lry > uly, GW_SCISSOR_REGION_EMPTY);
    // TODO fill/copy scissor is deranged

    state->scissor.ulx = ulx;
    state->scissor.uly = uly;
    state->scissor.lrx = lrx;
    state->scissor.lry = lry;

    if (state->cimg_set)
        return chk_scissor_cimg(state);
    return 0;
}

static int
chk_DPSetScissorFrac (gfx_state_t *state)
{
    int mode = gfxd_arg_value(0)->i;
    qu102_t ulx = gfxd_arg_value(1)->u;
    qu102_t uly = gfxd_arg_value(2)->u;
    qu102_t lrx = gfxd_arg_value(3)->u;
    qu102_t lry = gfxd_arg_value(4)->u;

    return chk_scissor(state, mode, ulx, uly, lrx, lry);
}

static int
chk_DPSetScissor (gfx_state_t *state)
{
    int mode = gfxd_arg_value(0)->i;
    unsigned ulx = gfxd_arg_value(1)->u;
    unsigned uly = gfxd_arg_value(2)->u;
    unsigned lrx = gfxd_arg_value(3)->u;
    unsigned lry = gfxd_arg_value(4)->u;

    return chk_scissor(state, mode, qu102(ulx), qu102(uly), qu102(lrx), qu102(lry));
}

static int
chk_DPSetTileSize (gfx_state_t *state)
{
    int tile = gfxd_arg_value(0)->i;
    qu102_t uls = gfxd_arg_value(1)->u;
    qu102_t ult = gfxd_arg_value(2)->u;
    qu102_t lrs = gfxd_arg_value(3)->u;
    qu102_t lrt = gfxd_arg_value(4)->u;

    tile_descriptor_t *tile_desc = get_tile_desc(state, tile);

    CHECK_TILESYNC(state, tile);

    ARG_CHECK(state, tile_desc != NULL, GW_TILEDESC_BAD);

    if(tile_desc != NULL)
    {
        tile_desc->uls = uls;
        tile_desc->ult = ult;
        tile_desc->lrs = lrs;
        tile_desc->lrt = lrt;
    }
    return 0;
}

static int
chk_SPBranchLessZraw (gfx_state_t *state)
{
    // TODO
    uint32_t branchdl = gfxd_arg_value(0)->u;
    int vtx = gfxd_arg_value(1)->i;
    int zval = gfxd_arg_value(2)->i;

    uint32_t branchdl_phys = segmented_to_physical(state, branchdl);

    chk_Range(state, branchdl_phys, sizeof(Gfx));

    // TODO this can be either a comparison with w or depth depending on whether the ucode is ZEX or not
    // if (vtx_depths[vtx] > 0x03FF || vtx_depths[vtx] <= zval)
    if (state->vtx_w[vtx] < (float)zval)
    {
        Note(gfxd_vprintf, "BranchLessZ success");
        state->gfx_addr = branchdl_phys - sizeof(Gfx);
    }
    return 0;
}

static int
chk_SPClipRatio (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_fog (int fm, int fo)
{
    // TODO

    return 0;
}

static int
chk_SPFogFactor (gfx_state_t *state)
{
    int fm = gfxd_arg_value(0)->i;
    int fo = gfxd_arg_value(1)->i;

    return chk_fog(fm, fo);
}

static int
chk_SPFogPosition (gfx_state_t *state)
{
    int min = gfxd_arg_value(0)->i;
    int max = gfxd_arg_value(1)->i;

    int fm = 128000 / ((max) - (min));
    int fo = (500 - (min)) * 256 / ((max) - (min));

    return chk_fog(fm, fo);
}

static int
chk_SPForceMatrix (gfx_state_t *state)
{
    uint32_t mptr = gfxd_arg_value(0)->u;
    uint32_t mptr_phys = segmented_to_physical(state, mptr);

    chk_Range(state, mptr_phys, sizeof(Mtx));

    // TODO

    return 0;
}

static int
chk_SPInsertMatrix (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
new_ucode (gfx_state_t *state, uint32_t uctext_start)
{
    gfx_ucode_registry_t *ucode_entry;

    for (ucode_entry = state->ucodes; ucode_entry->ucode != NULL; ucode_entry++)
    {
        if ((uctext_start & ~KSEG_MASK) == (ucode_entry->text_start & ~KSEG_MASK))
        {
            state->next_ucode = ucode_entry->ucode;
            return 0;
        }
    }
    WARNING_ERROR(state, GW_LOAD_UNRECOGNIZED_UCODE);
    return 1;
}

#define UCODE_TEXT_SIZE 0x1000
#define UCODE_DATA_SIZE 0x800

static int
chk_LoadUcode (gfx_state_t *state)
{
    unsigned uc_start = gfxd_arg_value(0)->u;
    unsigned uc_dsize = gfxd_arg_value(1)->u;

    uc_start &= 0x00FFFFFF;

    chk_Range(state, uc_start, UCODE_TEXT_SIZE);

    return new_ucode(state, uc_start);
}

static int
chk_SPLoadUcodeEx (gfx_state_t *state)
{
    unsigned uc_start = gfxd_arg_value(0)->u;
    unsigned uc_dstart = gfxd_arg_value(1)->u;
    unsigned uc_dsize = gfxd_arg_value(2)->u;

    uc_start &= 0x00FFFFFF;
    uc_dstart &= 0x00FFFFFF;

    chk_Range(state, uc_start, UCODE_TEXT_SIZE);
    chk_Range(state, uc_dstart, UCODE_DATA_SIZE);

    return new_ucode(state, uc_start);
}

static int
chk_SPLoadUcode (gfx_state_t *state)
{
    unsigned uc_start = gfxd_arg_value(0)->u;
    unsigned uc_dstart = gfxd_arg_value(1)->u;

    uc_start &= 0x00FFFFFF;
    uc_dstart &= 0x00FFFFFF;

    chk_Range(state, uc_start, UCODE_TEXT_SIZE);
    chk_Range(state, uc_dstart, UCODE_DATA_SIZE);

    return new_ucode(state, uc_start);
}

static int
chk_SPLookAtX (gfx_state_t *state)
{
    uint32_t l = gfxd_arg_value(0)->u;
    uint32_t l_phys = segmented_to_physical(state, l);

    chk_Range(state, l_phys, sizeof(Light));

    return 0;
}

static int
chk_SPLookAtY (gfx_state_t *state)
{
    uint32_t l = gfxd_arg_value(0)->u;
    uint32_t l_phys = segmented_to_physical(state, l);

    chk_Range(state, l_phys, sizeof(Light));

    return 0;
}

static int
chk_SPLookAt (gfx_state_t *state)
{
    uint32_t l = gfxd_arg_value(0)->u;
    uint32_t l_phys = segmented_to_physical(state, l);

    chk_Range(state, l_phys, sizeof(LookAt));

    return 0;
}

static int
chk_SPModifyVertex (gfx_state_t *state)
{
    int vtx = gfxd_arg_value(0)->i;
    unsigned where = gfxd_arg_value(1)->u;
    unsigned val = gfxd_arg_value(2)->u;

    ARG_CHECK(state, vtx < VTX_CACHE_SIZE, GW_MODIFYVTX_OOB);

    // TODO

    return 0;
}

static int
chk_SPPerspNormalize (gfx_state_t *state)
{
    int wscale = gfxd_arg_value(0)->u;
    state->persp_norm = ((float)wscale) / 0x10000;
    return 0;
}

static int
chk_popmtx (gfx_state_t *state, int param, int num)
{
    ARG_CHECK(state, param == G_MTX_MODELVIEW, GW_MTX_POP_NOT_MODELVIEW);
    ARG_CHECK(state, state->matrix_stack_depth >= num, GW_MTX_STACK_UNDERFLOW);

    state->matrix_stack_depth -= num;
    obstack_pop(&state->mtx_stack, num);
    return 0;
}

static int
chk_SPPopMatrix (gfx_state_t *state)
{
    int param = gfxd_arg_value(0)->i;

    return chk_popmtx(state, param, 1);
}

static int
chk_SPPopMatrixN (gfx_state_t *state)
{
    int param = gfxd_arg_value(0)->i;
    int num   = gfxd_arg_value(1)->i;

    return chk_popmtx(state, param, num);
}

static int
chk_SPSetLights1 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSetLights2 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSetLights3 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSetLights4 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSetLights5 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSetLights6 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSetLights7 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPNumLights (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPLight (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPLightColor (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPTexture (gfx_state_t *state)
{
    qu510_t sc = gfxd_arg_value(0)->i;
    qu510_t tc = gfxd_arg_value(1)->i;
    int level = gfxd_arg_value(2)->i;
    int tile = gfxd_arg_value(3)->i;
    int on = gfxd_arg_value(4)->i;

    tile_descriptor_t *tile_desc = get_tile_desc(state, tile);

    ARG_CHECK(state, tile_desc != NULL, GW_TILEDESC_BAD);

    state->render_tile = tile;

    if (tile_desc != NULL)
    {
        state->render_tile_on = on;
        state->render_tile_level = level;
        state->tex_s_scale = sc;
        state->tex_t_scale = tc;
    }
    return 0;
}

static int
chk_SPTextureRectangle (gfx_state_t *state)
{
    qu102_t ulx  = gfxd_arg_value(0)->u;
    qu102_t uly  = gfxd_arg_value(1)->u;
    qu102_t lrx  = gfxd_arg_value(2)->u;
    qu102_t lry  = gfxd_arg_value(3)->u;
    int tile     = gfxd_arg_value(4)->i;
    qs105_t s    = gfxd_arg_value(5)->i;
    qs105_t t    = gfxd_arg_value(6)->i;
    qs510_t dsdx = gfxd_arg_value(7)->i;
    qs510_t dtdy = gfxd_arg_value(8)->i;

    // Unlike triangles, this is an error as with textured triangles inverse W is always provided in the command,
    // however with texrects inverse W cannot be specified
    ARG_CHECK(state, OTHERMODE_VAL(state, hi, TEXTPERSP) == G_TP_NONE, GW_TEXRECT_PERSP_CORRECT);

    // TODO generate rdp texrect command

    return chk_render_primitive(state, PRIM_TYPE_TEXRECT, tile);
}

static int
chk_SPTextureRectangleFlip (gfx_state_t *state)
{
    qu102_t ulx  = gfxd_arg_value(0)->u;
    qu102_t uly  = gfxd_arg_value(1)->u;
    qu102_t lrx  = gfxd_arg_value(2)->u;
    qu102_t lry  = gfxd_arg_value(3)->u;
    int tile     = gfxd_arg_value(4)->i;
    qs105_t s    = gfxd_arg_value(5)->i;
    qs105_t t    = gfxd_arg_value(6)->i;
    qs510_t dsdx = gfxd_arg_value(7)->i;
    qs510_t dtdy = gfxd_arg_value(8)->i;

    ARG_CHECK(state, OTHERMODE_VAL(state, hi, TEXTPERSP) == G_TP_NONE, GW_TEXRECT_PERSP_CORRECT);

    // TODO generate rdp texrect command

    return chk_render_primitive(state, PRIM_TYPE_TEXRECT, tile);
}

static int
chk_BranchZ (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_DPHalf1 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_DPHalf2 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_DPWord (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_MoveWd (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_MoveMem (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPDma_io (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPDmaRead (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPDmaWrite (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPNoOp (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_Special3 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_Special2 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_Special1 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPBgRectCopy (gfx_state_t *state)
{
    uint32_t bg = gfxd_arg_value(0)->u;

    uint32_t bg_phys = segmented_to_physical(state, bg);

    chk_Range(state, bg_phys, sizeof(uObjBg));

    return 0;
}

static int
chk_SPBgRect1Cyc (gfx_state_t *state)
{
    uint32_t bg = gfxd_arg_value(0)->u;
    uint32_t bg_phys = segmented_to_physical(state, bg);

    chk_Range(state, bg_phys, sizeof(uObjBg));

    return 0;
}

static int
chk_SPObjRectangle (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjRectangleR (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjSprite (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjMatrix (gfx_state_t *state)
{
    uint32_t mtx = gfxd_arg_value(0)->u;
    uint32_t mtx_phys = segmented_to_physical(state, mtx);

    chk_Range(state, mtx_phys, sizeof(uObjMtx));

    return 0;
}

static int
chk_SPObjSubMatrix (gfx_state_t *state)
{
    uint32_t mtx = gfxd_arg_value(0)->u;
    uint32_t mtx_phys = segmented_to_physical(state, mtx);

    chk_Range(state, mtx_phys, sizeof(uObjSubMtx));

    return 0;
}

static int
chk_ObjMoveMem (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjRenderMode (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjLoadTxtr (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjLoadTxRect (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjLoadTxRectR (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPObjLoadTxSprite (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SelectDL (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSelectDL (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSelectBranchDL (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_DPHalf0 (gfx_state_t *state)
{
    // TODO

    return 0;
}

static int
chk_SPSetStatus (gfx_state_t *state)
{
    // TODO

    return 0;
}

static bool
ltb_width_valid (int width, int siz)
{
    int valid_16b_widths[] =
    {
        4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 64, 72, 76, 100, 108, 128, 
        144, 152, 164, 200, 216, 228, 256, 304, 328, 432, 456, 512, 684, 820, 912,
    };

    for (size_t i = 0; i < ARRAY_COUNT(valid_16b_widths); i++)
    {
        int nominal_width = valid_16b_widths[i];

        switch (siz)
        {
            case G_IM_SIZ_4b:
                nominal_width *= 4;
                break;
            case G_IM_SIZ_8b:
                nominal_width *= 2;
                break;
            case G_IM_SIZ_16b:
                break;
            case G_IM_SIZ_32b:
                nominal_width /= 2;
                break;
        }

        if (width == nominal_width)
            return true;
    }
    return false;
}

/**
 *  For a given width computes the maximum possible height
 *  Returns -1 for invalid widths, 0 for impossible widths, or > 0 the maximum height for that width
 */
static int
max_lines (int width, int siz)
{
    // gfxd_printf("width: %d\n", width);

    if (width == 0)
        return -1;

    // Convert width to 16b
    switch (siz)
    {
        case G_IM_SIZ_4b:
            width /= 4;
            break;
        case G_IM_SIZ_8b:
            width /= 2;
            break;
        case G_IM_SIZ_32b:
            width *= 2; // TODO ?
            break;
    }

    // gfxd_printf("width (16b txls): %d\n", width);

    int ldbits = G_SIZ_LDBITS(G_SIZ_LDSIZ(siz));
    int bytes_per_line = (width * ldbits) / 8;

    // gfxd_printf("line bytes: %d\n", bytes_per_line);

    if (bytes_per_line % 8 != 0 || bytes_per_line >= (8 * TMEM_SIZE) / ldbits)
        return -1; // invalid width

    int abs_max_lines = LOADBLOCK_MAX_TEXELS / width;
    int max_lines = 0;

    // gfxd_printf("abs max lines %d\n", abs_max_lines);

    if ((width & (width - 1)) == 0) // no error for power of 2 width
    {
        max_lines = abs_max_lines;

        // gfxd_printf("power of 2\n");
    }
    else
    {   // accumulating dxt builds up small error over time for non power of 2 values,
        // meaning the true max lines may be less than the amount that is required
        double int_part;
        int words_per_line = bytes_per_line / 8;
        float dxt = 2048 / (float)words_per_line;
        float err_per_line = words_per_line * (1.0f - modf(dxt, &int_part));
        max_lines = floorf(dxt / err_per_line);
        if (max_lines > abs_max_lines)
            max_lines = abs_max_lines;
    }

    // gfxd_printf("max lines (for load) %d\n", max_lines);

    // Convert the max lines to one in terms of the actual texel size
    switch (siz)
    {
        case G_IM_SIZ_4b:
            max_lines *= 4;
            break;
        case G_IM_SIZ_8b:
            max_lines *= 2;
            break;
        //case G_IM_SIZ_32b:
        //    max_lines /= 2;
        //    break;
    }

    // gfxd_printf("max lines: %d\n", max_lines);
    return max_lines;
}

static int
chk_LTB (gfx_state_t *state, uint32_t timg, int fmt, int siz, int width, int height, int pal, unsigned cms,
         unsigned cmt, int masks, int maskt, int shifts, int shiftt)
{
    // ARG_CHECK(state, ltb_dims_valid(width, height, siz), "Bad width-height combination for LoadTextureBlock");

    int mlines = max_lines(width, siz);

    // For LTB, not all width-height combinations are valid for loading. Check that the load is not corrupted.
    ARG_CHECK(state, mlines > 0, GW_LTB_INVAID_WIDTH);
    ARG_CHECK(state, mlines >= height, GW_LTB_DXT_CORRUPTION);

    // For LTB, the number of bytes in a (render tile) line must be an integral number of TMEM words since there is no
    // line padding performed when loading.
    // e.g. fmt=I siz=8b width=4 is invalid since (4*8)/8 is not a multiple of 8 
    int bytes_per_render_line = (width * G_SIZ_BITS(siz)) / 8;
    ARG_CHECK(state, bytes_per_render_line % 8 == 0, GW_LTB_INVAID_WIDTH);


    // ARG_CHECK(state, mlines, "Invalid width for LoadTextureBlock");
    // // TODO this needs fixing and promoting to an error
    // ARG_CHECK(state, mlines >= height,
    //           "The width-height combination for LoadTextureBlock will cause corruption due to dxt imprecision, "
    //           "a width of %d may only have at most %d texture lines, this has %d", width, mlines, height);

    if (state->options->print_textures)
    {
        tile_descriptor_t *tile_desc = get_tile_desc(state, pal);
        if (tile_desc != NULL)
            draw_last_timg(state, timg, fmt, siz, height, width, tile_desc->tmem, pal, tile_desc->lrs);
    }

    return 0;
}

static int
chk_DPLoadTextureBlock (gfx_state_t *state)
{
    uint32_t timg = gfxd_arg_value(0)->u;
    int fmt       = gfxd_arg_value(1)->i;
    int siz       = gfxd_arg_value(2)->i;
    int width     = gfxd_arg_value(3)->i;
    int height    = gfxd_arg_value(4)->i;
    int pal       = gfxd_arg_value(5)->i;
    unsigned cms  = gfxd_arg_value(6)->u;
    unsigned cmt  = gfxd_arg_value(7)->u;
    int masks     = gfxd_arg_value(8)->i;
    int maskt     = gfxd_arg_value(9)->i;
    int shifts    = gfxd_arg_value(10)->i;
    int shiftt    = gfxd_arg_value(11)->i;

    return chk_LTB(state, timg, fmt, siz, width, height, pal, cms, cmt, masks, maskt, shifts, shiftt);
}

static int
chk_DPLoadTextureBlock_4b (gfx_state_t *state)
{
    unsigned timg = gfxd_arg_value(0)->u;
    int fmt       = gfxd_arg_value(1)->i;
    int siz       = G_IM_SIZ_4b;
    int width     = gfxd_arg_value(2)->i;
    int height    = gfxd_arg_value(3)->i;
    int pal       = gfxd_arg_value(4)->i;
    unsigned cms  = gfxd_arg_value(5)->u;
    unsigned cmt  = gfxd_arg_value(6)->u;
    int masks     = gfxd_arg_value(7)->i;
    int maskt     = gfxd_arg_value(8)->i;
    int shifts    = gfxd_arg_value(9)->i;
    int shiftt    = gfxd_arg_value(10)->i;

    return chk_LTB(state, timg, fmt, siz, width, height, pal, cms, cmt, masks, maskt, shifts, shiftt);
}

/**********************************************************************************************************************\
 *  Run
 */

static chk_fn chk_tbl[] = {
    [gfxd_Invalid]                  = chk_Invalid,
    [gfxd_DPFillRectangle]          = chk_DPFillRectangle,
    [gfxd_DPFullSync]               = chk_DPFullSync,
    [gfxd_DPLoadSync]               = chk_DPLoadSync,
    [gfxd_DPTileSync]               = chk_DPTileSync,
    [gfxd_DPPipeSync]               = chk_DPPipeSync,
    [gfxd_DPLoadTLUT_pal16]         = NULL,
    [gfxd_DPLoadTLUT_pal256]        = NULL,
    [gfxd_DPLoadMultiBlockYuvS]     = NULL,
    [gfxd_DPLoadMultiBlockYuv]      = NULL,
    [gfxd_DPLoadMultiBlock_4bS]     = NULL,
    [gfxd_DPLoadMultiBlock_4b]      = NULL,
    [gfxd_DPLoadMultiBlockS]        = NULL,
    [gfxd_DPLoadMultiBlock]         = NULL,
    [gfxd__DPLoadTextureBlockYuvS]  = NULL,
    [gfxd__DPLoadTextureBlockYuv]   = NULL,
    [gfxd__DPLoadTextureBlock_4bS]  = NULL,
    [gfxd__DPLoadTextureBlock_4b]   = NULL,
    [gfxd__DPLoadTextureBlockS]     = NULL,
    [gfxd__DPLoadTextureBlock]      = NULL,
    [gfxd_DPLoadTextureBlockYuvS]   = NULL,
    [gfxd_DPLoadTextureBlockYuv]    = NULL,
    [gfxd_DPLoadTextureBlock_4bS]   = NULL,
    [gfxd_DPLoadTextureBlock_4b]    = chk_DPLoadTextureBlock_4b,
    [gfxd_DPLoadTextureBlockS]      = NULL,
    [gfxd_DPLoadTextureBlock]       = chk_DPLoadTextureBlock,
    [gfxd_DPLoadMultiTileYuv]       = NULL,
    [gfxd_DPLoadMultiTile_4b]       = NULL,
    [gfxd_DPLoadMultiTile]          = NULL,
    [gfxd__DPLoadTextureTileYuv]    = NULL,
    [gfxd__DPLoadTextureTile_4b]    = NULL,
    [gfxd__DPLoadTextureTile]       = NULL,
    [gfxd_DPLoadTextureTileYuv]     = NULL,
    [gfxd_DPLoadTextureTile_4b]     = NULL,
    [gfxd_DPLoadTextureTile]        = NULL,
    [gfxd_DPLoadBlock]              = chk_DPLoadBlock,
    [gfxd_DPNoOp]                   = NULL,
    [gfxd_DPNoOpTag]                = NULL,
    [gfxd_DPNoOpTag3]               = NULL,
    [gfxd_DPPipelineMode]           = chk_DPPipelineMode,
    [gfxd_DPSetBlendColor]          = chk_DPSetBlendColor,
    [gfxd_DPSetEnvColor]            = chk_DPSetEnvColor,
    [gfxd_DPSetFillColor]           = chk_DPSetFillColor,
    [gfxd_DPSetFogColor]            = chk_DPSetFogColor,
    [gfxd_DPSetPrimColor]           = chk_DPSetPrimColor,
    [gfxd_DPSetColorImage]          = chk_DPSetColorImage,
    [gfxd_DPSetDepthImage]          = chk_DPSetDepthImage,
    [gfxd_DPSetTextureImage]        = chk_DPSetTextureImage,
    [gfxd_DPSetAlphaCompare]        = chk_DPSetAlphaCompare,
    [gfxd_DPSetAlphaDither]         = chk_DPSetAlphaDither,
    [gfxd_DPSetColorDither]         = chk_DPSetColorDither,
    [gfxd_DPSetCombineMode]         = chk_DPSetCombine,
    [gfxd_DPSetCombineLERP]         = chk_DPSetCombine,
    [gfxd_DPSetConvert]             = chk_DPSetConvert,
    [gfxd_DPSetTextureConvert]      = chk_DPSetTextureConvert,
    [gfxd_DPSetCycleType]           = chk_DPSetCycleType,
    [gfxd_DPSetDepthSource]         = chk_DPSetDepthSource,
    [gfxd_DPSetCombineKey]          = chk_DPSetCombineKey,
    [gfxd_DPSetKeyGB]               = chk_DPSetKeyGB,
    [gfxd_DPSetKeyR]                = chk_DPSetKeyR,
    [gfxd_DPSetPrimDepth]           = chk_DPSetPrimDepth,
    [gfxd_DPSetRenderMode]          = chk_DPSetRenderMode,
    [gfxd_DPSetScissor]             = chk_DPSetScissor,
    [gfxd_DPSetScissorFrac]         = chk_DPSetScissorFrac,
    [gfxd_DPSetTextureDetail]       = chk_DPSetTextureDetail,
    [gfxd_DPSetTextureFilter]       = chk_DPSetTextureFilter,
    [gfxd_DPSetTextureLOD]          = chk_DPSetTextureLOD,
    [gfxd_DPSetTextureLUT]          = chk_DPSetTextureLUT,
    [gfxd_DPSetTexturePersp]        = chk_DPSetTexturePersp,
    [gfxd_DPSetTile]                = chk_DPSetTile,
    [gfxd_DPSetTileSize]            = chk_DPSetTileSize,
    [gfxd_SP1Triangle]              = chk_SP1Triangle,
    [gfxd_SP2Triangles]             = chk_SP2Triangles,
    [gfxd_SP1Quadrangle]            = chk_SP1Quadrangle,
    [gfxd_SPBranchLessZraw]         = chk_SPBranchLessZraw,
    [gfxd_SPBranchList]             = chk_SPBranchList,
    [gfxd_SPClipRatio]              = chk_SPClipRatio,
    [gfxd_SPCullDisplayList]        = chk_SPCullDisplayList,
    [gfxd_SPDisplayList]            = chk_SPDisplayList,
    [gfxd_SPEndDisplayList]         = chk_SPEndDisplayList,
    [gfxd_SPFogFactor]              = chk_SPFogFactor,
    [gfxd_SPFogPosition]            = chk_SPFogPosition,
    [gfxd_SPForceMatrix]            = chk_SPForceMatrix,
    [gfxd_SPSetGeometryMode]        = chk_SPSetGeometryMode,
    [gfxd_SPClearGeometryMode]      = chk_SPClearGeometryMode,
    [gfxd_SPLoadGeometryMode]       = chk_SPLoadGeometryMode,
    [gfxd_SPInsertMatrix]           = chk_SPInsertMatrix,
    [gfxd_SPLine3D]                 = chk_SPLine3D,
    [gfxd_SPLineW3D]                = chk_SPLineW3D,
    [gfxd_SPLoadUcode]              = chk_SPLoadUcode,
    [gfxd_SPLookAtX]                = chk_SPLookAtX,
    [gfxd_SPLookAtY]                = chk_SPLookAtY,
    [gfxd_SPLookAt]                 = chk_SPLookAt,
    [gfxd_SPMatrix]                 = chk_SPMatrix,
    [gfxd_SPModifyVertex]           = chk_SPModifyVertex,
    [gfxd_SPPerspNormalize]         = chk_SPPerspNormalize,
    [gfxd_SPPopMatrix]              = chk_SPPopMatrix,
    [gfxd_SPPopMatrixN]             = chk_SPPopMatrixN,
    [gfxd_SPSegment]                = chk_SPSegment,
    [gfxd_SPSetLights1]             = chk_SPSetLights1,
    [gfxd_SPSetLights2]             = chk_SPSetLights2,
    [gfxd_SPSetLights3]             = chk_SPSetLights3,
    [gfxd_SPSetLights4]             = chk_SPSetLights4,
    [gfxd_SPSetLights5]             = chk_SPSetLights5,
    [gfxd_SPSetLights6]             = chk_SPSetLights6,
    [gfxd_SPSetLights7]             = chk_SPSetLights7,
    [gfxd_SPNumLights]              = chk_SPNumLights,
    [gfxd_SPLight]                  = chk_SPLight,
    [gfxd_SPLightColor]             = chk_SPLightColor,
    [gfxd_SPTexture]                = chk_SPTexture,
    [gfxd_SPTextureRectangle]       = chk_SPTextureRectangle,
    [gfxd_SPTextureRectangleFlip]   = chk_SPTextureRectangleFlip,
    [gfxd_SPVertex]                 = chk_SPVertex,
    [gfxd_SPViewport]               = chk_SPViewport,
    [gfxd_DPLoadTLUTCmd]            = chk_DPLoadTLUTCmd,
    [gfxd_DPLoadTLUT]               = NULL,
    [gfxd_BranchZ]                  = chk_BranchZ,
    [gfxd_DisplayList]              = chk_DisplayList,
    [gfxd_DPHalf1]                  = chk_DPHalf1,
    [gfxd_DPHalf2]                  = chk_DPHalf2,
    [gfxd_DPWord]                   = chk_DPWord,
    [gfxd_DPLoadTile]               = chk_DPLoadTile,
    [gfxd_SPGeometryMode]           = chk_SPGeometryMode,
    [gfxd_SPSetOtherMode]           = chk_SPSetOtherMode,
    [gfxd_SPSetOtherModeLo]         = chk_SPSetOtherModeLo,
    [gfxd_SPSetOtherModeHi]         = chk_SPSetOtherModeHi,
    [gfxd_DPSetOtherMode]           = chk_DPSetOtherMode,
    [gfxd_MoveWd]                   = chk_MoveWd,
    [gfxd_MoveHalfWd]               = chk_MoveWd,
    [gfxd_MoveMem]                  = chk_MoveMem,
    [gfxd_SPDma_io]                 = chk_SPDma_io,
    [gfxd_SPDmaRead]                = chk_SPDmaRead,
    [gfxd_SPDmaWrite]               = chk_SPDmaWrite,
    [gfxd_LoadUcode]                = chk_LoadUcode,
    [gfxd_SPLoadUcodeEx]            = chk_SPLoadUcodeEx,
    [gfxd_TexRect]                  = NULL,
    [gfxd_TexRectFlip]              = NULL,
    [gfxd_SPNoOp]                   = chk_SPNoOp,
    [gfxd_Special3]                 = chk_Special3,
    [gfxd_Special2]                 = chk_Special2,
    [gfxd_Special1]                 = chk_Special1,
    [gfxd_SPBgRectCopy]             = chk_SPBgRectCopy,
    [gfxd_SPBgRect1Cyc]             = chk_SPBgRect1Cyc,
    [gfxd_SPObjRectangle]           = chk_SPObjRectangle,
    [gfxd_SPObjRectangleR]          = chk_SPObjRectangleR,
    [gfxd_SPObjSprite]              = chk_SPObjSprite,
    [gfxd_SPObjMatrix]              = chk_SPObjMatrix,
    [gfxd_SPObjSubMatrix]           = chk_SPObjSubMatrix,
    [gfxd_ObjMoveMem]               = chk_ObjMoveMem,
    [gfxd_SPObjRenderMode]          = chk_SPObjRenderMode,
    [gfxd_SPObjLoadTxtr]            = chk_SPObjLoadTxtr,
    [gfxd_SPObjLoadTxRect]          = chk_SPObjLoadTxRect,
    [gfxd_SPObjLoadTxRectR]         = chk_SPObjLoadTxRectR,
    [gfxd_SPObjLoadTxSprite]        = chk_SPObjLoadTxSprite,
    [gfxd_SelectDL]                 = chk_SelectDL,
    [gfxd_SPSelectDL]               = chk_SPSelectDL,
    [gfxd_SPSelectBranchDL]         = chk_SPSelectBranchDL,
    [gfxd_DPHalf0]                  = chk_DPHalf0,
    [gfxd_SPSetStatus]              = chk_SPSetStatus,

    // F3DEX3
    [gfxd_SPRelSegment]             = chk_SPRelSegment,
    [gfxd_SPMemset]                 = chk_SPMemset,
};

#define STRING_COLOR VT_RGB256COL(130) //VT_SGR("0;92") // green

#define ZDZ_UNPACK_Z(x)     (((x) >> 2) & 0x3FFF)
#define ZDZ_UNPACK_DZ(x)    (((x) >> 0) & 3)

#define RGBA16_R(x) (((x) >> 11) & 0x1F)
#define RGBA16_G(x) (((x) >>  6) & 0x1F)
#define RGBA16_B(x) (((x) >>  1) & 0x1F)
#define RGBA16_A(x) (((x) >>  0) & 1)

#define RGBA16_EXPAND(x)    (((x) << 3) | ((x) >> 2))

static void
arg_handler (int arg_num)
{
    // No checking is done here, it is left entirely to command handlers
    int m_id = gfxd_macro_id();
    int arg_type = gfxd_arg_type(arg_num);
    gfx_state_t *state = (gfx_state_t *)gfxd_udata_get();

    if (gfxd_arg_callbacks(arg_num) != 0)
        return;

    const gfxd_value_t *arg_value;

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
        case gfxd_RelSegptr:
        case gfxd_Lightsn:
        case gfxd_Lightptr:
        case gfxd_Vtxptr:
        case gfxd_Vpptr:
        case gfxd_SpritePtr:
        case gfxd_BgPtr:
        case gfxd_ObjMtxptr:
        case gfxd_ObjTxtr:
        case gfxd_ObjTxSprite:
            arg_value = gfxd_arg_value(arg_num);

            gfxd_print_value(arg_type, arg_value);

            // Print converted address
            uint32_t ram_addr = segmented_to_kseg0(state, arg_value->u);
            if (ram_addr != arg_value->u)
                gfxd_printf(" /* 0x%08X */", ram_addr);
            break;
        case gfxd_Color:
            if (m_id == gfxd_DPSetFillColor && state->cimg_set)
            {
                const char *color_fmt = (state->options->hex_color)
                                      ? "0x%02X, 0x%02X, 0x%02X, 0x%02X"
                                      : "%d, %d, %d, %d";
                uint32_t color = gfxd_arg_value(arg_num)->u;

                // TODO decimal vs hexadecimal colors
                switch (FMT_SIZ(state->last_cimg.fmt, state->last_cimg.siz))
                {
                    case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_16b):
                        if (state->zimg_set && state->last_cimg.addr == state->last_zimg.addr)
                            // color and depth images match, write as zdz
                            gfxd_printf("(GPACK_ZDZ(0x%X, 0x%X) << 16) | GPACK_ZDZ(0x%X, 0x%X)",
                                        ZDZ_UNPACK_Z(color >> 16), ZDZ_UNPACK_DZ(color >> 16),
                                        ZDZ_UNPACK_Z(color >>  0), ZDZ_UNPACK_DZ(color >>  0));
                        else
                        {
                            // otherwise write as rgba16
                            gfxd_puts("(GPACK_RGBA5551(");
                            gfxd_printf(color_fmt,
                                        RGBA16_EXPAND(RGBA16_R(color >> 16)), RGBA16_EXPAND(RGBA16_G(color >> 16)),
                                        RGBA16_EXPAND(RGBA16_B(color >> 16)), 255 * RGBA16_A(color >> 16));
                            gfxd_puts(") << 16) | GPACK_RGBA5551(");
                            gfxd_printf(color_fmt,
                                        RGBA16_EXPAND(RGBA16_R(color >>  0)), RGBA16_EXPAND(RGBA16_G(color >>  0)),
                                        RGBA16_EXPAND(RGBA16_B(color >>  0)), 255 * RGBA16_A(color >>  0));
                            gfxd_puts(")");
                        }
                        break;
                    case FMT_SIZ(G_IM_FMT_RGBA, G_IM_SIZ_32b):
                    case FMT_SIZ(G_IM_FMT_I, G_IM_SIZ_8b):
                    case FMT_SIZ(G_IM_FMT_CI, G_IM_SIZ_8b):
                        if (state->options->hex_color)
                            gfxd_printf("%08X", color);
                        else
                        {
                            // this macro isn't really part of the sdk.. but better than %08X ?
                            gfxd_puts("GPACK_RGBA8888(");
                            gfxd_printf(color_fmt,
                                        (color >> 24) & 0xFF, (color >> 16) & 0xFF,
                                        (color >>  8) & 0xFF, (color >>  0) & 0xFF);
                            gfxd_puts(")");
                        }
                        break;
                }
                break;
            }
            FALLTHROUGH;
        default:
            gfxd_arg_dflt(arg_num);
            break;
    }
}

static void
decode_noop_cmd (gfx_state_t *state)
{
    const gfxd_value_t *noop_type = gfxd_arg_value(0 /* type */);
    const gfxd_value_t *noop_data = gfxd_arg_value(1 /* data */);
    const gfxd_value_t *noop_data1 = gfxd_arg_value(2 /* data1 */);

    switch (noop_type->u)
    {
        // TODO these break dynamic macros
        case 1:
            gfxd_printf("gsDPNoOpHere(" STRING_COLOR);
            print_string(state, segmented_to_physical(state, noop_data->u), gfx_fprintf_wrapper, NULL);
            gfxd_printf(VT_RST ", 0x%04X)", noop_data->u, noop_data1->u);
            break;
        case 2:
            gfxd_printf("gsDPNoOpString(" STRING_COLOR);
            print_string(state, segmented_to_physical(state, noop_data->u), gfx_fprintf_wrapper, NULL);
            gfxd_printf(VT_RST ", 0x%04X)",  noop_data1->u);
            break;
        case 3:
            gfxd_printf("gsDPNoOpWord(0x%08X, 0x%04X)", noop_data->u, noop_data1->u);
            break;
        case 4:
            gfxd_printf("gsDPNoOpFloat(0x%08X, 0x%04X)", noop_data->u, noop_data1->u);
            break;
        case 5:
            if (noop_data->u != 0)
                goto emit_noop_tag3;

            if (noop_data1->u == 0)
                gfxd_printf("gsDPNoOpQuiet()");
            else
                gfxd_printf("gsDPNoOpVerbose(0x%04X)", noop_data1->u);
            break;
        case 6:
            gfxd_printf("gsDPNoOpCallBack(0x%08X, 0x%04X)", noop_data->u, noop_data1->u);
            break;
        case 7:
            gfxd_printf(VT_FGCOL(GREEN) "gsDPNoOpOpenDisp" VT_RST "(" STRING_COLOR);
            print_string(state, segmented_to_physical(state, noop_data->u), gfx_fprintf_wrapper, NULL);
            gfxd_printf(VT_RST ", %d)",  noop_data1->u);

            DispEntry open_ent = {
                .str_addr = segmented_to_physical(state, noop_data->u),
                .line_no = noop_data1->u,
            };
            obstack_push(&state->disp_stack, &open_ent);
            break;
        case 8:
            gfxd_printf(VT_FGCOL(RED) "gsDPNoOpCloseDisp" VT_RST "(" STRING_COLOR);
            print_string(state, segmented_to_physical(state, noop_data->u), gfx_fprintf_wrapper, NULL);
            gfxd_printf(VT_RST ", %d)",  noop_data1->u);

            obstack_pop(&state->disp_stack, 1);
            break;
        default:
emit_noop_tag3:
            gfxd_printf("%s(0x%02X, 0x%08X, 0x%04X)", gfxd_macro_name(), noop_type->u, noop_data->u, noop_data1->u);
            WARNING_ERROR(state, GW_UNK_NOOP_TAG3);
            break;
    }
}

static void
macro_print (void)
{
    unsigned int m_id = gfxd_macro_id();
    gfx_state_t *state = (gfx_state_t *)gfxd_udata_get();

    if (m_id == gfxd_DPNoOpTag3)
    {
        /* decode special NoOps */
        decode_noop_cmd(state);
    }
    else
    {
        const char *name = gfxd_macro_name();
        if (name == NULL)
        {
            gfxd_puts("(Gfx){");
        }
        else
        {
            static const char *macro_colors[] =
                {
                    [gfxd_SPDisplayList] = VT_FGCOL(CYAN),
                    [gfxd_SPEndDisplayList] = VT_FGCOL(PURPLE),
                };

            const char *color = NULL;
            if (m_id < ARRAY_COUNT(macro_colors))
                color = macro_colors[m_id];

            if (color != NULL)
                gfxd_puts(color);

            gfxd_puts(name);
            gfxd_puts(VT_RST "(");
        }

        int n_arg = gfxd_arg_count();
        for (int i = 0; i < n_arg; i++)
        {
            if (i != 0)
                gfxd_puts(", ");

            arg_handler(i);
        }

        gfxd_puts((name == NULL) ? "}" : ")");
    }
}

static int
do_single_gfx (void)
{
    int m_id = gfxd_macro_id();
    gfx_state_t *state = (gfx_state_t *)gfxd_udata_get();

    if (state->multi_packet && state->options->print_multi_packet)
    {
        gfxd_puts("            ");
        macro_print();
        gfxd_puts(",\n");
    }

    chk_fn fn = chk_tbl[m_id];
    if (fn != NULL)
        fn(state);

    // Tile busy

    for (int i = 0; i < 8; i++)
    {
        if (state->tile_busy[i] > 0)
        {
            // If tile is marked busy by a command, increment busy counter
            state->tile_busy[i]++;
            if (state->tile_busy[i] >= 2)
            {
                // Been busy for long enough, release the tile.
                // Supposedly tiles are not busy for long
                state->tile_busy[i] = 0;
            }
        }
    }

    return 0;
}

static int
macro_fn (void)
{
    int m_id = gfxd_macro_id();
    gfx_state_t *state = (gfx_state_t *)gfxd_udata_get();

    gfxd_printf("  /* %6d %08X */  ", state->n_gfx, state->gfx_addr);
    macro_print();
    gfxd_puts(",\n");

    int n_pkt = gfxd_macro_packets();

    if (n_pkt == 1) {
        // single-packet

        do_single_gfx();
    } else {
        // Run check function for the multi-packet command
        chk_fn fn = chk_tbl[m_id];
        if (fn != NULL)
            fn(state);

        // multi-packet handling
        strncpy(state->multi_packet_name, gfxd_macro_name(), sizeof(state->multi_packet_name)-1);
        state->multi_packet = true;
        if (m_id != gfxd_SPTextureRectangle && m_id != gfxd_SPTextureRectangleFlip)
            // gfxd_printf("In expansion of %s:\n", gfxd_macro_name());
            gfxd_foreach_pkt(do_single_gfx);
            // gfxd_puts("[done]\n");

        state->multi_packet = false;
    }

    state->last_gfx_pkt_count = n_pkt;
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
    gfx_state_t *state = gfxd_udata_get();

    state->rdram->seek(state->gfx_addr);
    return state->rdram->read(buf, 1, count);
}

/**************************************************************************
 *  Main
 */

int
analyze_gbi (FILE *print_out, gfx_ucode_registry_t *ucodes, gbd_options_t *opts, rdram_interface_t *rdram,
             const void *rdram_arg, struct start_location_info *start_location)
{
    gfx_state_t state = {
        .task_done = false,
        .pipeline_crashed = false,
        .multi_packet = false,

        .last_gfx_pkt_count = 1,

        .segment_table[0] = 0,
        .segment_set_bits = 1,
        .matrix_stack_depth = 0,
        .matrix_projection_set = false,
        .matrix_modelview_set = false,

        .ex3_mat_cull_mode = 0,

        .sp_dram_stack_size = 0x400, // TODO this is configurable

        .render_tile = G_TX_RENDERTILE,
        .render_tile_on = false,
        .last_loaded_vtx_num = 0,
        .persp_norm = 1.0,
        .geometry_mode = G_CLIPPING,
        .dl_stack_top = -1,
        .scissor_set = false,
        .othermode_hi = 0,
        .othermode_lo = 0,
        .combiner_hi = 0,
        .combiner_lo = 0,
        .pipe_busy = false,
        .tile_busy = { 0 },
        .load_busy = false,
        .fill_color = 0,
        .fill_color_set = false,
    };

    state.ucodes = ucodes;
    state.rdram = rdram;
    state.options = opts;

    if (state.rdram->open(rdram_arg))
    {
        fprintf(print_out, ERROR_COLOR "FAILED to open RDRAM image" VT_RST "\n");
        goto err;
    }

    uint32_t start_addr = -1U;
    switch (start_location->type)
    {
        case USE_START_ADDR_AT_POINTER: {
            uint32_t auto_start_addr;
            if (!state.rdram->read_at(&auto_start_addr, start_location->start_location_ptr & ~KSEG_MASK, sizeof(uint32_t)))
            {
                fprintf(print_out, ERROR_COLOR "FAILED to read start address from pointer 0x%08" PRIx32 VT_RST "\n", start_location->start_location_ptr);
                goto err;
            }
            start_addr = BSWAP32(auto_start_addr);
        } break;
        case USE_GIVEN_START_ADDR: {
            start_addr = start_location->start_location;
        } break;
        default:
            fprintf(print_out, ERROR_COLOR "FAILED to get start address, unknown type %d" VT_RST "\n", (int)start_location->type);
            goto err;
    }
    start_addr &= ~KSEG_MASK;
    if (!state.rdram->seek(start_addr))
    {
        fprintf(print_out, ERROR_COLOR "FAILED to seek to start address" VT_RST "\n");
        goto err;
    }
    state.gfx_addr = start_addr;

    obstack_new(&state.disp_stack, sizeof(DispEntry));
    obstack_new(&state.mtx_stack, sizeof(MtxF));
    MtxF zero_mf = { 0 };
    obstack_push(&state.mtx_stack, &zero_mf);

    gfxd_input_callback(input_callback);
    gfxd_output_fd(fileno(print_out));

    gfxd_udata_set(&state);

    gfxd_disable(gfxd_stop_on_invalid);
    gfxd_enable(gfxd_stop_on_end);
    gfxd_enable(gfxd_emit_ext_macro);

    if (!state.options->hex_color)
        gfxd_enable(gfxd_emit_dec_color);
    else
        gfxd_disable(gfxd_emit_dec_color);

    if (state.options->q_macros)
        gfxd_enable(gfxd_emit_q_macro);
    else
        gfxd_disable(gfxd_emit_q_macro);

    gfxd_arg_fn(arg_handler);

    gfxd_endian(gfxd_endian_big, 4);
    gfxd_macro_fn(macro_fn);

    state.next_ucode = state.ucodes[0].ucode;
    gfxd_target(state.next_ucode);

    state.n_gfx = 0;
    while (!state.task_done && !state.pipeline_crashed && gfxd_execute() == 1)
    {
        state.n_gfx++;
        state.gfx_addr += state.last_gfx_pkt_count * sizeof(Gfx);
        gfxd_target(state.next_ucode);

        if (state.options->line != 0 && state.n_gfx == state.options->line)
        {
            state.task_done = true;
            break;
        }
    }

    if (state.task_done)
    {
        fprintf(print_out, "Graphics task completed successfully.\n");
    }
    else
    {
        fprintf(print_out, "\n        " ERROR_COLOR "Graphics Task has CRASHED" VT_RST "\n\n");

        // Print state information

        uint32_t cur_dl = dl_stack_peek(&state);

        fprintf(print_out, "In Display List ");
        if (cur_dl == 0)
            fprintf(print_out, "ROOT\n");
        else
        {
            fprintf(print_out, "0x%08X (command #%d)\n", cur_dl, state.n_gfx - 1);
            if (state.dl_stack_top > 0)
            {
                // print a display list stack trace if more than 1 dl deep
                fprintf(print_out, "Stack Trace:\n");
                for (int i = state.dl_stack_top; i > 0; i--)
                    fprintf(print_out, "    %08X\n", state.dl_stack[i]);
            }
        }

        fprintf(print_out, "DISP refs trace:\n");
        VECTOR_FOR_EACH_ELEMENT_REVERSED(&state.disp_stack.v, ent, DispEntry*)
        {
            fprintf(print_out, "    At ");
            print_string(&state, ent->str_addr, fprintf, print_out);
            fprintf(print_out, ", %d\n", ent->line_no);
        }

        fprintf(print_out, "Segment dump:\n");
        print_segments(print_out, state.segment_table);

    }

    fflush(print_out);

    // TODO output more information here
    // for verbose outputs, the maximum depth reached on the DL stack might be useful, also
    //  the percentage of the gfxpool that gets used by this graphics task?
    // also output any warnings and what the cause of the crash is if applicable

    state.rdram->close();
    return 0;
err:
    state.rdram->close();
    return -1;
}
