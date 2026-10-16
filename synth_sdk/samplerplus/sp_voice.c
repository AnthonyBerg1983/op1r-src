/* sampler+: stock synth sampler voice model (docs/46) */
#include "sp_voice.h"

static inline int32_t sat32(int64_t x) { return x > 0x7FFFFFFF ? 0x7FFFFFFF : x < -0x7FFFFFFFLL - 1 ? (int32_t)0x80000000 : (int32_t)x; }

/* x = hi << 16 + (lo << 16) >>> 15 (saturated), then the firmware's MAC sequence x * 0x8132.4099 / 2^15 (~1.0093):
 * a fraction of the 6 s buffer -> pos (same as the drum voice's bound, docs/31) */
int32_t sp_bound(int16_t hi_w, int16_t lo_w)
{
    int32_t x = sat32((int64_t)((int32_t)hi_w << 16) + (((int32_t)lo_w << 16) >> 15));
    int32_t h = x >> 16;
    uint32_t l = (uint32_t)x & 0xFFFF;
    int64_t a1 = ((int64_t)(l >> 1) + (int64_t)l * 0x4099 + (int64_t)h * 0x8000) >> 15;
    return sat32((int64_t)h * 0x8132 + a1);
}

/* mult_fr1x32x32(inc, rate) >> 7: Q31 product of the phase increment and the buffer rate */
int32_t sp_step(int32_t inc, int32_t rate)
{
    int32_t ih = inc >> 16, rh = rate >> 16;
    uint32_t il = (uint32_t)inc & 0xFFFF, rl = (uint32_t)rate & 0xFFFF;
    int64_t lo = (int64_t)((il * rl) >> 16) + (int64_t)ih * rl + (int64_t)rh * il;
    return sat32((int64_t)ih * rh * 2 + (lo >> 15)) >> 7;
}

int16_t sp_tap(const int16_t *data, const int16_t *fir, int32_t pos, int32_t g)
{
    int32_t ph = (pos & 0xFFF) >> 6;
    const int16_t *s = data + (pos >> 12), *c = fir - ph;
    int32_t acc = 0;
#ifdef __bfin__                                                   /* one fractional multiply + saturating add per tap */
    for (int k = 0; k < 16; k++) acc = __builtin_bfin_add_fr1x32(acc, __builtin_bfin_mult_fr1x32(c[k * 64], s[k]));
#else
    for (int k = 0; k < 16; k++) {                                /* saturating MAC (the DSP's W32 MAC) */
        int32_t p = (int32_t)c[k * 64] * s[k], r;
        if (p == 0x40000000) p = 0x3FFFFFFF;
        if (__builtin_add_overflow(acc, 2 * p, &r)) r = acc < 0 ? (int32_t)0x80000000 : 0x7FFFFFFF;
        acc = r;
    }
#endif
    int32_t y = (acc >> 16) * g * 2 + (int32_t)(((uint32_t)acc & 0xFFFF) * (uint32_t)g >> 15);   /* g >= 0 */
    y = y > 0x07FFFFFF ? 0x7FFFFFFF : y < -0x08000000 ? (int32_t)0x80000000 : y * 16;           /* << 4, sat */
    return (int16_t)(y >> 16);
}

void sp_stock_note_on(sp_voice *v, const int16_t *k)
{
    v->released = 0; v->first = 1;
    v->pos = sp_bound(k[4] >> 14 ? k[3] : k[0], 0);               /* reverse: from the end */
}
void sp_stock_note_off(sp_voice *v) { v->released = 1; }

void sp_stock_render(sp_voice *v, int16_t *out, const int16_t *k, const int32_t *inc, int32_t rate,
                     const int16_t *data, const int16_t *fir)
{
    int32_t start = sp_bound(k[0], 0);
    if (v->first) { if (v->pos < start) v->pos = start; v->first = 0; }
    int32_t ls = sp_bound(k[1], k[5]), le = sp_bound(k[2], k[6]);
    int32_t g = ((int32_t)k[7] * k[7] * 2 + 0x8000) >> 16; if (g > 32767) g = 32767;
    int loop = k[0] < k[2] && !v->released;
    if (!(k[4] >> 14)) {
        int32_t end = sp_bound(k[3], 0);
        for (int n = 0; n < SP_BLOCK; n++) {
            if (loop && v->pos >= le) v->pos = ls;
            if (v->pos < end) { out[n] = sp_tap(data, fir, v->pos, g); v->pos = sat32((int64_t)v->pos + sp_step(inc[n], rate)); }
            else out[n] = 0;
        }
    } else {
        for (int n = 0; n < SP_BLOCK; n++) {
            if (loop && v->pos < ls) v->pos = le;
            if (v->pos < start) out[n] = 0;
            else { out[n] = sp_tap(data, fir, v->pos, g); v->pos = sat32((int64_t)v->pos - sp_step(inc[n], rate)); }
        }
    }
}
