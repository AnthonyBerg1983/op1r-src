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
static const uint8_t c_ring[4] = { 0x65, 0x65, 0x79, 0xFF },   /* the pan ring's grey: pan selected */
                     c_sel[4] = { 0xAE, 0xB1, 0xDC, 0xFF };     /* aux send selected for Shift + turn */
static const int32_t ring_cx[4] = { 43068, 124988, 204178, 283368 };   /* panring1..4 cx (mixer.svg) in Q10 */

void mx_draw_sends(void *r, void *scr)
{
    for (int t = 0; t < 4; t++) {
        int32_t cx = ring_cx[t], cy = 90 * 1024;            /* under pan ring t */
        for (int i = 0; i < 16; i++) {
            op1_pt a = { cx + ring[i][0], cy + ring[i][1] }, b = { cx + ring[(i + 1) & 15][0], cy + ring[(i + 1) & 15][1] };
            op1_raster_line(r, &a, &b, 0x600, mx_pan[t] ? c_ring : c_sel);
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

#ifndef OP1_HOST
static void mx_update(void *scr, int dt)
{
    int aux = fr_src == 2;
    if (aux != mx_small) mx_geometry(scr, aux);
    mx_stock(scr, dt);
    if (aux) mx_draw_sends(op1_raster(), scr);
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
