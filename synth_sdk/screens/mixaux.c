/* Mixer T1 with FX2 = aux (docs/54): the stock MixerScreen (screen 0x1F, mixer.svg) keeps drawing; its vtable is
 * copied with update replaced: while aux is on, the four volume texts ("lev1..4", MixerScreen +0x88[t]) are halved
 * and moved under a send knob (unlabelled ring + pointer in the text's colour) drawn below each pan knob. The texts
 * are re-laid out by the stock text_set (0x01A61AAC) only when the string changes, so after changing their geometry
 * the string is cleared and the stock draw sets the digits again. Aux off: original geometry, stock page. */
#include "../op1_synth.h"

extern uint8_t fr_src, fr_send[4];
uint8_t mx_pan[4];                                          /* encoder press: 1 = Shift + turn sets pan (stock), 0 = aux send (default) */
typedef void (*mx_update_t)(void *, int);
static mx_update_t mx_stock;
static uint32_t mx_vt[12];
static int32_t mx_orig[4][4];                               /* x +0x58, y +0x5C, size +0x64, scale +0x68 */
static uint8_t mx_saved, mx_small;

#ifdef OP1_HOST
void *mx_host_text[4];
#define TEXT(scr, t) mx_host_text[t]
void mx_host_text_set(void *o, const char *s);
#define TEXT_SET(o, s) mx_host_text_set(o, s)
#else
#define TEXT(scr, t) (((void **)(scr))[0x88 / 4 + (t)])
#define TEXT_SET(o, s) op1_text_set(o, s)
#endif
#define F(o, off) (*(int32_t *)((uint8_t *)(o) + (off)))

static void mx_geometry(void *scr, int small)
{
    if (!mx_saved) {
        for (int t = 0; t < 4; t++) { void *o = TEXT(scr, t); mx_orig[t][0] = F(o, 0x58); mx_orig[t][1] = F(o, 0x5C); mx_orig[t][2] = F(o, 0x64); mx_orig[t][3] = F(o, 0x68); }
        mx_saved = 1;
    }
    for (int t = 0; t < 4; t++) {
        void *o = TEXT(scr, t); const int32_t *g = mx_orig[t];
        if (small) {                                        /* stock box 85..139 (size s px): half size, top at 112 */
            F(o, 0x58) = g[0] + g[2] * 1024 * 27 / 100;     /* about centred for two digits */
            F(o, 0x5C) = g[1] + g[2] / 2;
            F(o, 0x64) = g[2] / 2; F(o, 0x68) = g[3] / 2;
        } else { F(o, 0x58) = g[0]; F(o, 0x5C) = g[1]; F(o, 0x64) = g[2]; F(o, 0x68) = g[3]; }
        TEXT_SET(o, "");                                    /* re-layout at the next stock text_set */
    }
    mx_small = (uint8_t)small;
}

static const int16_t ring[16][2] = { { 0, -10836 }, { 4147, -10011 }, { 7662, -7662 }, { 10011, -4147 }, { 10836, 0 },
    { 10011, 4147 }, { 7662, 7662 }, { 4147, 10011 }, { 0, 10836 }, { -4147, 10011 }, { -7662, 7662 }, { -10011, 4147 },
    { -10836, 0 }, { -10011, -4147 }, { -7662, -7662 }, { -4147, -10011 } };
static const int16_t ptr[21][2] = { { -5720, 5720 }, { -6898, 4227 }, { -7694, 2500 }, { -8065, 635 }, { -7990, -1265 },
    { -7474, -3096 }, { -6545, -4755 }, { -5254, -6151 }, { -3673, -7208 }, { -1888, -7866 }, { 0, -8090 }, { 1888, -7866 },
    { 3673, -7208 }, { 5254, -6151 }, { 6545, -4755 }, { 7474, -3096 }, { 7990, -1265 }, { 8065, 635 }, { 7694, 2500 },
    { 6898, 4227 }, { 5720, 5720 } };                       /* send 0..100 in steps of 5: -135..+135 deg, r 7.9 */
/* colours: not selected = the stock pan grey, selected (encoder press) = a bit brighter, Shift = the stock Shift
 * highlight, on the selected control only (pan elements recoloured per element, docs/16) */
static const uint8_t c_dark[4] = { 0x65, 0x65, 0x79, 0xFF }, c_sel[4] = { 0x8C, 0x8C, 0xA4, 0xFF };
#define C_HL      ((const uint8_t *)0x01BAAF70)             /* stock Shift colour of "pangroup" (0x019E5998) */
#define C_HL_LR   ((const uint8_t *)0x01BAAF6C)             /* .. of "panleftright" */
#define SHIFT     (*(volatile uint8_t *)0x01DD7F78)
static uint8_t mx_aux_col[4][4];                            /* send ring colour per track, set in mx_update */
static const int32_t ring_cx[4] = { 43068, 124988, 204178, 283368 };   /* panring1..4 cx (mixer.svg) in Q10 */

void mx_draw_sends(void *r, void *scr)
{
    for (int t = 0; t < 4; t++) {
        int32_t cx = ring_cx[t], cy = 95 * 1024;            /* under pan ring t (66), a gap below L / R */
        for (int i = 0; i < 16; i++) {
            op1_pt a = { cx + ring[i][0], cy + ring[i][1] }, b = { cx + ring[(i + 1) & 15][0], cy + ring[(i + 1) & 15][1] };
            op1_raster_line(r, &a, &b, 0x600, mx_aux_col[t]);
        }
        int v = fr_send[t], k = v / 5, f = v % 5, k1 = k < 20 ? k + 1 : 20;
        op1_pt c = { cx, cy }, p = { cx + ptr[k][0] + (ptr[k1][0] - ptr[k][0]) * f / 5, cy + ptr[k][1] + (ptr[k1][1] - ptr[k][1]) * f / 5 };
#ifdef OP1_HOST
        static const uint8_t col[4][4] = { { 0x69, 0x8E, 0xFF, 0xFF }, { 0, 0xED, 0x95, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0x3A, 0x5D, 0xFF } };
        const uint8_t *cc = col[t]; (void)scr;
#else
        const uint8_t *cc = (const uint8_t *)TEXT(scr, t) + 0x6C;   /* the digits' colour (grey when muted) */
#endif
        op1_raster_line(r, &c, &p, 0x600, cc);
    }
}

static void col4(uint8_t *d, const uint8_t *c) { d[0] = c[0]; d[1] = c[1]; d[2] = c[2]; d[3] = c[3]; }

#ifndef OP1_HOST
/* The stock draw colours all four pan knobs at once through the "pangroup" / "panleftright" group styles
 * (MixerScreen +0x40 / +0x44, 0x019E5998, by Shift); the groups override the elements' own styles (tried: per
 * element colours do not show). While aux is on, the stock writes go to a dummy, the group styles are kept dark, and
 * the selected (encoder press) pan knobs are drawn over in their colour: ring, top tick, pointer, L, R. */
static uint8_t mx_dummy[2][4], mx_style_orig[2][4], mx_styled;
static uint8_t *mx_style[2];

static void mx_pan_styles(void *scr, int on)
{
    uint32_t *o = (uint32_t *)scr;
    if (!mx_style[0]) {
        mx_style[0] = (uint8_t *)o[0x40 / 4]; mx_style[1] = (uint8_t *)o[0x44 / 4];
        col4(mx_style_orig[0], mx_style[0]); col4(mx_style_orig[1], mx_style[1]);
    }
    o[0x40 / 4] = on ? (uint32_t)mx_dummy[0] : (uint32_t)mx_style[0];
    o[0x44 / 4] = on ? (uint32_t)mx_dummy[1] : (uint32_t)mx_style[1];
    for (int g = 0; g < 2; g++) col4(mx_style[g], on ? c_dark : mx_style_orig[g]);
    mx_styled = (uint8_t)on;
}

static float fsin(float a)                                  /* |a| <= pi: reduced to |a| <= pi/2, Taylor 7 */
{
    const float PI = 3.14159265f;
    if (a > PI / 2) a = PI - a; else if (a < -PI / 2) a = -PI - a;
    float a2 = a * a;
    return a * (1 - a2 / 6 * (1 - a2 / 20 * (1 - a2 / 42)));
}

static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t *c)
{
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, 0x600, c);
}

static void mx_draw_pan(void *r, int t, const uint8_t *c, const uint8_t *c_lr)
{
    int32_t cx = ring_cx[t], cy = 66 * 1024 - 3;            /* panring cy 65.997 */
    for (int i = 0; i < 16; i++)
        ln(r, cx + ring[i][0], cy + ring[i][1], cx + ring[(i + 1) & 15][0], cy + ring[(i + 1) & 15][1], c);
    ln(r, cx, 48467, cx, 51197, c);                         /* panmid: 47.331 .. 49.997 */
    const float *v = *(const float **)((uint8_t *)((void *(*)(void))0x01A29AD8)() + 4);   /* ReadInterface: pan = v[4 + t] */
    float a = v[4 + t] * 4.18879f;                          /* the stock rotation: (4 v / 3) pi */
    a = a > 3.14159f ? 3.14159f : a < -3.14159f ? -3.14159f : a;
    float sn = fsin(a), cs = fsin(a + (a > 1.5708f ? -4.71239f : 1.5708f));
    ln(r, cx, cy, cx + (int32_t)(13653 * sn), cy - (int32_t)(13653 * cs), c);   /* pan pointer, 13.33 long */
    int32_t lx = cx - 24576, rx = cx + 19113;               /* L at cx - 24, R at cx + 18.665 (mixer.svg) */
    ln(r, lx, cy, lx, 78504, c_lr); ln(r, lx, 78504, lx + 8192, 78504, c_lr);
    static const int16_t rp[7][2] = { { 0, 10920 }, { 0, 0 }, { 5462, 0 }, { 7373, 820 }, { 8192, 2731 }, { 7373, 4641 }, { 5462, 5462 } };
    for (int i = 0; i < 6; i++) ln(r, rx + rp[i][0], cy + rp[i][1], rx + rp[i + 1][0], cy + rp[i + 1][1], c_lr);
    ln(r, rx + 5462, cy + 5462, rx + 8193, cy + 10920, c_lr);   /* R leg */
    ln(r, rx + 5462, cy + 5462, rx, cy + 5462, c_lr);           /* rline */
}

static void mx_update(void *scr, int dt)
{
    int aux = fr_src == 2;
    if (aux != mx_small) mx_geometry(scr, aux);
    if (aux != mx_styled) mx_pan_styles(scr, aux);
    int sh = SHIFT != 0;
    if (aux) for (int t = 0; t < 4; t++) col4(mx_aux_col[t], mx_pan[t] ? c_dark : sh ? C_HL : c_sel);
    mx_stock(scr, dt);
    if (!aux) return;
    void *r = op1_raster();
    for (int t = 0; t < 4; t++) if (mx_pan[t]) mx_draw_pan(r, t, sh ? C_HL : c_sel, sh ? C_HL_LR : c_sel);
    mx_draw_sends(r, scr);
}

void mx_attach(void *scr)                                   /* boot: the stock mixer object, before it is drawn */
{
    if (!scr || mx_stock) return;
    const uint32_t *vt = ((const uint32_t **)scr)[3];
    for (int i = 0; i < 12; i++) mx_vt[i] = vt[i];
    mx_stock = (mx_update_t)vt[7];
    mx_vt[7] = (uint32_t)mx_update;
    ((uint32_t *)scr)[3] = (uint32_t)mx_vt;
}
#endif
