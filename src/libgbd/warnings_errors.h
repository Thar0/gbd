DEFINE_ERROR(ADDR_NOT_IN_RDRAM, "Address not in rdram")
DEFINE_ERROR(RANGE_NOT_IN_RDRAM, "Data does not fit fully in rdram")
DEFINE_ERROR(CULLING_BAD_INDICES, "vn should be greater than v0")
DEFINE_ERROR(CULLING_VERTS_OOB, "Vertices indexed out-of-bounds")
DEFINE_ERROR(SEGZERO_NONZERO, "Assigning segment 0 to something other than 0x00000000")
DEFINE_ERROR(INVALID_SEGMENT_NUM, "Invalid segment number")
DEFINE_ERROR(INVALID_SEGMENT_NUM_REL, "Invalid relative segment number") /* EX3 */
DEFINE_ERROR(MTX_PUSHED_TO_PROJECTION, "Cannot push to the projection matrix stack")
DEFINE_ERROR(MTX_STACK_OVERFLOW, "Matrix stack overflow")
DEFINE_ERROR(MUL_PROJECTION_UNSET, "Multiplying a projection matrix when no projection matrix was loaded")
DEFINE_ERROR(MUL_MODELVIEW_UNSET, "Multiplying a modelview matrix when no modelview matrix was loaded")
DEFINE_ERROR(SCISSOR_TOO_WIDE, "Scissor region is too wide for color image")
DEFINE_ERROR(SCISSOR_START_INVALID, "Scissor region start address not in RDRAM")
DEFINE_ERROR(SCISSOR_END_INVALID, "Scissor region end address not in RDRAM")
DEFINE_ERROR(INVALID_CIMG_FMT, "Invalid image format")
DEFINE_ERROR(BAD_CIMG_ALIGNMENT, "Color image alignment must be 64-byte")
DEFINE_ERROR(INVALID_CIMG_FMTSIZ, "Bad format for color image, should be RGBA16, RGBA32 or I8")
DEFINE_ERROR(BAD_ZIMG_ALIGNMENT, "Depth image alignment must be 64-byte")
DEFINE_ERROR(INVALID_TIMG_FMT, "Invalid texture image format")
DEFINE_ERROR(INVALID_TIMG_FMTSIZ, "Invalid texture image format/size combination")
DEFINE_ERROR(VTX_LOADING_ZERO, "Vertex count cannot be zero")
DEFINE_ERROR(VTX_LOADING_TOO_MANY, "Loading too many vertices")
DEFINE_ERROR(VTX_CACHE_OVERFLOW, "Loading %d vertices at position %d overflows the vertex cache")
DEFINE_ERROR(FILLMODE_4B, "Rendering primitives to a 4-bit color image is prohibited in FILL mode")
DEFINE_ERROR(COPYMODE_32B, "Rendering primitives to a 32-bit color image is prohibited in COPY mode")
DEFINE_ERROR(SCISSOR_UNSET, "Scissor must be set before rendering primitives")
DEFINE_ERROR(CIMG_UNSET, "Color image must be set before rendering primitives")
DEFINE_ERROR(FILLRECT_FILLCOLOR_UNSET, "Filling a rectangle without ever setting the fill color")
DEFINE_ERROR(CC_SHADE_INVALID, "Shade used in CC cycle %d %s input when %s")
DEFINE_ERROR(CC_SHADE_ALPHA_INVALID, "Shade alpha used as blender cycle %d input when %s")
DEFINE_ERROR(ZS_PIXEL_SET_WITHOUT_G_ZBUFFER, "Per-pixel depth source (G_ZS_PIXEL) is set but G_ZBUFFER is unset")
DEFINE_ERROR(
    ZSRC_INVALID,
    "Per-pixel depth source is only available to triangles, either disable z-buffering or set G_ZS_PRIM in othermodes")
DEFINE_ERROR(CC_COMBINED_IN_C1, "COMBINED input selected for CC 1-Cycle %s")
DEFINE_ERROR(CC_COMBINED_ALPHA_IN_C1, "COMBINED_ALPHA input selected for CC 1-Cycle RGB")
DEFINE_ERROR(CC_COMBINED_IN_C2_C1, "COMBINED input selected for CC 2-Cycle Cycle 1 %s")
DEFINE_ERROR(CC_COMBINED_ALPHA_IN_C2_C1, "COMBINED_ALPHA input selected for CC 2-Cycle Cycle 1 RGB")
DEFINE_ERROR(FILLMODE_CIMG_ZIMG_RD_PER_PIXEL, "Color and depth image reading is prohibited in FILL mode")
DEFINE_ERROR(FILLMODE_ZIMG_WR_PER_PIXEL, "Per-pixel depth image updates are prohibited in FILL mode")
DEFINE_ERROR(COPYMODE_CIMG_ZIMG_RD_PER_PIXEL, "Color and depth image reading is prohibited in COPY mode")
DEFINE_ERROR(COPYMODE_ZIMG_WR_PER_PIXEL, "Per-pixel depth image updates are prohibited in COPY mode")
DEFINE_ERROR(COPYMODE_AA, "Anti-aliasing is unavailable in COPY mode")
DEFINE_ERROR(COPYMODE_BL_SET, "Blender pipeline stages are skipped in COPY mode")
DEFINE_ERROR(COPYMODE_TEXTURE_FILTER, "Texture filtering is unavailable in COPY mode")
DEFINE_ERROR(TILEDESC_BAD, "bad tile descriptor")
DEFINE_ERROR(CI_RENDER_TILE_NO_TLUT,
             "Render tile is color-indexed but TLUT mode was not enabled in other modes before drawing")
DEFINE_ERROR(NO_CI_RENDER_TILE_TLUT,
             "Render tile is not color-indexed but TLUT mode was enabled in other modes before drawing")
DEFINE_ERROR(COPYMODE_MISMATCH_8B, "4b and 8b images can only be copied to an 8b color image")
DEFINE_ERROR(COPYMODE_MISMATCH_16B, "16b images can only be copied to a 16b color image")
DEFINE_ERROR(TRI_VTX_OOB, "triangle %d indexed out of bounds vertices")
DEFINE_ERROR(BAD_TIMG_ALIGNMENT, "Texture image alignment will hang the RDP")
DEFINE_ERROR(LOADBLOCK_TOO_MANY_TEXELS, "LoadBlock only allows loading up to 2048 texels")
DEFINE_ERROR(TIMG_LOAD_4B, "Loading with a 4-bit texture image is unsupported")
DEFINE_ERROR(TIMG_TILE_LOAD_NONMATCHING,
             "Texture image and texture tile format/size do not match during load operation")
DEFINE_ERROR(TLUT_TOO_LARGE, "TLUTs can be at most 256 colors")
DEFINE_ERROR(TLUT_BAD_FMT, "TLUT format should be RGBA16 or IA16")
DEFINE_ERROR(TLUT_BAD_TMEM_ADDR, "A TLUT must be loaded into the high half of TMEM")
DEFINE_ERROR(TLUT_BAD_COORDS, "LoadTLUT loads nothing (on hardware, crashes on emulator) for lrt > ult")
DEFINE_ERROR(TIMG_BAD_TMEM_ADDR, "format %s requires address in low TMEM (< 0x800)")
DEFINE_ERROR(SCISSOR_REGION_EMPTY, "Scissor region is empty")
DEFINE_ERROR(MODIFYVTX_OOB, "Indexing out of bounds vertex")
DEFINE_ERROR(MTX_POP_NOT_MODELVIEW, "Can only pop from the modelview matrix stack")
DEFINE_ERROR(MTX_STACK_UNDERFLOW, "Matrix stack underflow")
DEFINE_ERROR(TEXRECT_PERSP_CORRECT, "Rectangles rendered with texture perspective correction")
DEFINE_ERROR(DL_STACK_OVERFLOW, "Display list stack overflow")
DEFINE_ERROR(INVALID_GFX_CMD, "Invalid gfx commands encountered")
DEFINE_ERROR(LOAD_UNRECOGNIZED_UCODE, "Loading unrecognized ucode")
DEFINE_ERROR(TRI_IN_FILLMODE, "Rendering triangles in fillmode is very likely to crash")
DEFINE_ERROR(LTB_INVAID_WIDTH, "Load texture block invalid width")
DEFINE_ERROR(LTB_DXT_CORRUPTION, "Load texture block dxt corruption")
DEFINE_ERROR(FULLSYNC_SENT, "DPFullSync should always be the last RDP command executed in a task")
DEFINE_WARNING(MISSING_PIPESYNC, "Missing pipesync")
DEFINE_WARNING(MISSING_LOADSYNC, "Missing loadsync")
DEFINE_WARNING(MISSING_TILESYNC, "Missing tilesync")
DEFINE_WARNING(SUPERFLUOUS_PIPESYNC, "Superfluous pipesync")
DEFINE_WARNING(SUPERFLUOUS_LOADSYNC, "Superfluous loadsync")
DEFINE_WARNING(SUPERFLUOUS_TILESYNC, "Superfluous tilesync")
DEFINE_WARNING(UNSET_SEGMENT, "Using segment %d before it was assigned")
DEFINE_WARNING(UNK_DL_VARIANT, "Unknown display list command variant, will act as %s")
DEFINE_WARNING(UNK_NOOP_TAG3, "Unknown gsDPNoOpTag3 variant, possibly garbage data")
DEFINE_WARNING(CULLING_BAD_VERTS, "Volume culling references vertices that were not loaded in the last batch")
DEFINE_WARNING(DANGEROUS_TEXTURE_ALIGNMENT, "texture image is not 8-byte aligned; this has the potential to hang the "
                                            "RDP, it is recommended to align textures to 8 bytes")
DEFINE_WARNING(BLENDER_SET_BUT_UNUSED,
               "Blend formula is configured however the blender is not used as both AA_EN and FORCE_BL are unset")
DEFINE_WARNING(BLENDER_STAGES_DIFFER_1CYC,
               "Blender configuration differs between stages in 1-Cycle mode, first cycle configuration is ignored")
DEFINE_WARNING(
    CC_STAGES_DIFFER_1CYC,
    "Color combiner configuration differs between stages in 1-Cycle mode, first cycle configuration is ignored")
DEFINE_WARNING(
    CC_TEXEL1_RGB_1CYC,
    "TEXEL1 input selected for CC 1-Cycle RGB, this reads the next pixel TEXEL0 instead of the current pixel TEXEL1")
DEFINE_WARNING(
    CC_TEXEL1_ALPHA_1CYC,
    "TEXEL1 input selected for CC 1-Cycle Alpha, this reads the next pixel TEXEL0 instead of the current pixel TEXEL1")
DEFINE_WARNING(CC_TEXEL1_RGBA_1CYC, "TEXEL1_ALPHA input selected for CC 1-Cycle RGB")
DEFINE_WARNING(
    CC_TEXEL1_RGB_C2_2CYC,
    "TEXEL1 input selected for CC Cycle 2 RGB, this reads the next pixel TEXEL0 instead of the current pixel TEXEL1")
DEFINE_WARNING(
    CC_TEXEL1_ALPHA_C2_2CYC,
    "TEXEL1 input selected for CC Cycle 2 Alpha, this reads the next pixel TEXEL0 instead of the current pixel TEXEL1")
DEFINE_WARNING(CC_TEXEL1_RGBA_C2_2CYC, "TEXEL1_ALPHA input selected for CC Cycle 2 RGB, this reads the next pixel "
                                       "TEXEL0_ALPHA instead of the current pixel TEXEL1_ALPHA")
DEFINE_WARNING(TRI_LEECHING_VERTS, "triangle %d references vertices that were not loaded in the last batch")
DEFINE_WARNING(TRI_TXTR_NOPERSP, "Textured triangles rendered without texture perspective correction")
DEFINE_WARNING(TEX_CI8_NONZERO_PAL, "Palette is non-zero for CI8 tile descriptor, will be treated as 0")
DEFINE_WARNING(
    RDP_LOG2_INACCURATE,
    "The log2 that RDP hardware computes for dz does not agree with the true log2 of dz, inaccuracy may result.")
DEFINE_WARNING(TEXRECT_IN_FILLMODE, "Rendering textured rectangles in fill mode act like filled rectangles")
DEFINE_WARNING(CVG_SAVE_NO_IM_RD,
               "cvg_dst mode set to SAVE but IM_RD is not set, will behave as if cvg_dst was set to FULL")
DEFINE_WARNING(CI_CIMG_FMTSIZ, "CI8 is technically invalid for color images, it behaves the same as I8 which better describes the behavior")
