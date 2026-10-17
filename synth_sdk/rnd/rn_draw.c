/* Synth randomiser panel (rn_draw.h): the page faded to half, then the panel; raster lines have flat ends, so the
 * fill is 2 px rows and each corner two chords. */
#ifndef OP1_HOST
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#endif
#include "../op1_synth.h"
#include "rn_draw.h"

#define Q(v)        OP1_Q10(v)
#define RN_X0       80                      /* panel, px */
#define RN_X1       240
#define RN_Y0       56
#define RN_Y1       104
#define RN_RAD      4                       /* corner radius, px */
#define RN_BW       0x800                   /* border, Q10: 2 px */
#define RN_TXT_SIZE 24                      /* stock Text size */
#define RN_TXT_Y    68                      /* text top, px */
#define RN_C45      2896                    /* cos 45 deg, Q12 */
#ifdef OP1_HOST
void host_text_align(void *t, int align);
#define TXT_CTOR(o, x, y, c, a) (host_text_ctor((o), (x), (y), RN_TXT_SIZE, (c)), host_text_align((o), (a)))
#define FADE(r) host_raster_dim(r)                              /* (host raster: lines only, the call is logged) */
#else
#define TXT_CTOR(o, x, y, c, a) (((void *(*)(void *, int32_t, int32_t, int32_t, const uint8_t *, int32_t))0x01A61904)( \
                                    (o), (x), (y), RN_TXT_SIZE, (c), (a)))           /* align: 0 left, 1 centred, 2 right */
#define FADE(r) rn_fade(r)
#endif

static const uint8_t c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_black[4] = { 0, 0, 0, 0xFF };
static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
static uint32_t txt[2][OP1_TEXT_SIZE / 4];  /* lowercase: the stock font's capitals */
static uint8_t txt_made;

static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)   /* Q10 */
{
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, w, c);
}
/* every pixel at half (Raster::dim_all leaves bright ones at ~16%): Raster +0 pixels (RGB565), +4 width, +8 stride
 * (bytes), +0x0C height */
typedef struct { uint16_t *px; uint32_t w, stride, h; } rn_raster;
void rn_fade(void *r)
{
    const rn_raster *ra = r;
    uint8_t *row = (uint8_t *)ra->px;
    for (uint32_t y = 0; y < ra->h; y++, row += ra->stride & ~1u)
        for (uint32_t x = 0; x < ra->w; x++) ((uint16_t *)row)[x] = (uint16_t)(((uint16_t *)row)[x] >> 1 & 0x7BEF);
}
/* rounded corner around (cx, cy): two chords from (dx0, dy0) to (dx1, dy1) */
static void corner(void *r, int32_t cx, int32_t cy, int dx0, int dy0, int dx1, int dy1)
{
    int32_t R = Q(RN_RAD), m = R * RN_C45 >> 12;
    int32_t ax = cx + dx0 * R, ay = cy + dy0 * R, bx = cx + (dx0 + dx1) * m, by = cy + (dy0 + dy1) * m;
    ln(r, ax, ay, bx, by, RN_BW, c_white);
    ln(r, bx, by, cx + dx1 * R, cy + dy1 * R, RN_BW, c_white);
}

void rn_draw_panel(void *r, int back)
{
    if (!txt_made) {
        TXT_CTOR(txt[0], Q((RN_X0 + RN_X1) / 2), RN_TXT_Y, c_white, 1); op1_text_set(txt[0], "random");
        TXT_CTOR(txt[1], Q((RN_X0 + RN_X1) / 2), RN_TXT_Y, c_white, 1); op1_text_set(txt[1], "modnar");
        txt_made = 1;
    }
    FADE(r);
    int32_t x0 = Q(RN_X0), x1 = Q(RN_X1), y0 = Q(RN_Y0), y1 = Q(RN_Y1), R = Q(RN_RAD);
    for (int32_t y = y0; y < y1; y += Q(2)) {                 /* fill */
        int32_t in = y < y0 + Q(2) || y + Q(2) > y1 - Q(2) ? Q(2) : y < y0 + R || y + Q(2) > y1 - R ? Q(1) : 0;
        ln(r, x0 + in, y + Q(1), x1 - in, y + Q(1), Q(2), c_black);
    }
    ln(r, x0 + R, y0, x1 - R, y0, RN_BW, c_white);            /* border */
    ln(r, x0 + R, y1, x1 - R, y1, RN_BW, c_white);
    ln(r, x0, y0 + R, x0, y1 - R, RN_BW, c_white);
    ln(r, x1, y0 + R, x1, y1 - R, RN_BW, c_white);
    corner(r, x0 + R, y0 + R, -1, 0, 0, -1);
    corner(r, x1 - R, y0 + R, 0, -1, 1, 0);
    corner(r, x1 - R, y1 - R, 1, 0, 0, 1);
    corner(r, x0 + R, y1 - R, 0, 1, -1, 0);
    op1_text_draw(txt[back ? 1 : 0], r, ident);
}
