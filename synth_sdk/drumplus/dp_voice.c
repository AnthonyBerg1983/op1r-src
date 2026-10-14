/* drum+: stock drum voice model (DrumSampler render 0x019789EC, note_on 0x01978926, note_off 0x01978898; docs/31).
 * Matches the firmware bit for bit (harness/dp_voice_test.c against an emulator dump). */
#include "dp_voice.h"

static inline int32_t sat32(int64_t x) { return x > 0x7FFFFFFF ? 0x7FFFFFFF : x < -0x7FFFFFFFLL - 1 ? (int32_t)0x80000000 : (int32_t)x; }
static inline int16_t sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

/* region bound from the pad's hi / lo words (start: +2 / +0xA, end: +4 / +0xC), as the firmware's MAC sequence
 * (note_on 0x01978926): x = hi << 16 + (lo << 16) >>> 15 (saturated), then x * 0x40998000 / 2^30 (~1.0093) */
int32_t dp_bound(int16_t hi_w, int16_t lo_w)
{
    int32_t x = sat32((int64_t)((int32_t)hi_w << 16) + (((int32_t)lo_w << 16) >> 15));
    int32_t h = x >> 16;
    uint32_t l = (uint32_t)x & 0xFFFF;
    int64_t a1 = ((int64_t)((l * 0x8000u) >> 16) + (int64_t)l * 0x4099 + (int64_t)h * 0x8000) >> 15;
    return sat32((int64_t)h * 0x4099 * 2 + a1);
}

void dp_stock_note_on(dp_voice *v, const dp_pad *pad)
{
    v->state = DP_HELD; v->blocks = 0;                            /* reverse (pad+8 >> 14): from the end */
    v->pos = pad->reverse >> 14 ? dp_bound(pad->end_hi, pad->end_lo) : dp_bound(pad->start_hi, pad->start_lo);
}
void dp_stock_note_off(dp_voice *v, const dp_pad *pad)
{
    v->state = (pad->playmode >> 13) == 1 ? DP_RELEASED : DP_OFF;     /* one-shot plays on, gate stops */
}

int16_t dp_tap_sample(const int16_t *data, const int16_t *fir, int32_t pos, int32_t g)
{
    int32_t ph = (pos & 0xFFF) >> 6;
    const int16_t *s = data + (pos >> 11), *c = fir - ph;
    int32_t acc = 0;
    for (int k = 0; k < 16; k++) {                                /* saturating MAC, 32-bit (as the DSP's W32 MAC) */
        int32_t p = (int32_t)c[k * 64] * s[k], r;
        if (p == 0x40000000) p = 0x3FFFFFFF;                      /* -32768 * -32768: 2 * p would overflow */
        if (__builtin_add_overflow(acc, 2 * p, &r)) r = acc < 0 ? (int32_t)0x80000000 : 0x7FFFFFFF;
        acc = r;
    }
    /* (acc * g) >> 15 without a 64-bit multiply: g >= 0 (from squares), split acc into hi / lo 16 bits */
    int32_t y = (acc >> 16) * g * 2 + (int32_t)(((uint32_t)acc & 0xFFFF) * (uint32_t)g >> 15);
    y = y > 0x07FFFFFF ? 0x7FFFFFFF : y < -0x08000000 ? (int32_t)0x80000000 : y * 16;   /* << 4, saturated */
    return (int16_t)(y >> 16);
}

void dp_stock_render(dp_voice *v, int16_t *out, const dp_pad *pad, const int32_t *inc, const int16_t *vel,
                     const int16_t *data, const int16_t *fir)
{
    int32_t start = dp_bound(pad->start_hi, pad->start_lo), end = dp_bound(pad->end_hi, pad->end_lo);
    int loop = (pad->playmode >> 13) == 2 && v->state == DP_HELD, rev = pad->reverse >> 14, done = 0;
    for (int n = 0; n < DP_BLOCK; n++) {
        int32_t g = dp_gain(pad->volume, vel[n]);
        int32_t step = inc[n] >> 17;
        if (!rev) {
            if (!loop && v->pos >= end) { out[n] = 0; done = 1; continue; }
            out[n] = dp_tap_sample(data, fir, v->pos, g);
            int32_t p = sat32((int64_t)v->pos + step);
            v->pos = loop && p >= end ? start : p;
        } else {
            if (!loop && v->pos < start) { out[n] = 0; done = 1; continue; }
            out[n] = dp_tap_sample(data, fir, v->pos, g);
            int32_t p = sat32((int64_t)v->pos - step);
            v->pos = loop && p < start ? end : p;
        }
    }
    if (done) v->state = DP_OFF;
    v->blocks++;
}
