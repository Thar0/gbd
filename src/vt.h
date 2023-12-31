#ifndef VT_H_
#define VT_H_

#define VT_COLOR_BLACK         0
#define VT_COLOR_RED           1
#define VT_COLOR_GREEN         2
#define VT_COLOR_YELLOW        3
#define VT_COLOR_BLUE          4
#define VT_COLOR_PURPLE        5
#define VT_COLOR_CYAN          6
#define VT_COLOR_WHITE         7
#define VT_COLOR_LIGHTGRAY     8
#define VT_COLOR_DARKGRAY      9

#define VT_COLOR_FOREGROUND    3
#define VT_COLOR_BACKGROUND    4

#define STRINGIFY(x) #x

#define VT_COLOR_EXPAND0(type, color)  #type #color
#define VT_COLOR_EXPAND1(type, color)  VT_COLOR_EXPAND0(type, color)
#define VT_COLOR(type, color)  VT_COLOR_EXPAND1(VT_COLOR_##type, VT_COLOR_##color)

#define VT_ESC "\x1b"
#define VT_CSI "["
#define VT_CUP(x, y) VT_ESC VT_CSI y ";" x "H"
#define VT_ED(n) VT_ESC VT_CSI #n "J"
#define VT_SGR(n) VT_ESC VT_CSI n "m"

#define VT_COL(back, fore) VT_SGR(VT_COLOR(BACKGROUND, back) ";" VT_COLOR(FOREGROUND, fore))
#define VT_FGCOL(color) VT_SGR(VT_COLOR(FOREGROUND, color))
#define VT_BGCOL(color) VT_SGR(VT_COLOR(BACKGROUND, color))
#define VT_RST VT_SGR("")
#define VT_CLS VT_ED(2)

#define VT_RGBCOL_S(sb, sf) \
        VT_SGR("48;2;" sb) \
        VT_SGR("38;2;" sf)

#define VT_RGBFCOL_S(sf) \
        VT_SGR("38;2;" sf)

#define VT_RGBCOL(rb, gb, bb, rf, gf, bf) \
        VT_RGBCOL_S(#rb ";" #gb ";" #bb, #rf ";" #gf ";" #bf)

#define VT_RGBFCOL(rf, gf, bf) \
        VT_RGBFCOL_S(#rf ";" #gf ";" #bf)

#define VT_RGB256COL(n) \
        VT_SGR("38;5;" #n)

#endif
