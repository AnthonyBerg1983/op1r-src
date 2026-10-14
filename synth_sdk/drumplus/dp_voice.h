/* drum+: model of the stock OP-1 drum voice (DrumSampler, docs/31), bit-exact against the firmware (emulator dump),
 * as the base of the drum+ voice. Integer only. */
#ifndef DP_VOICE_H
#define DP_VOICE_H
#include <stdint.h>

#define DP_BLOCK 128
enum { DP_OFF = 0, DP_HELD = 1, DP_RELEASED = 2 };               /* voice state (+0xC of the stock voice) */

typedef struct {                                                 /* the stock voice's fields */
    int32_t pos;                                                 /* 1/2048 sample of the buffer (incl. guard) */
    int32_t state;
    uint32_t blocks;
} dp_voice;

/* pad entry: the 8 words of the live pad table (docs/27) */
typedef struct { int16_t pitch, start_hi, end_hi, playmode, reverse, start_lo, end_lo, volume; } dp_pad;

/* fir: the firmware's interpolation table centre (0xFF8021D2 on the OP-1: 16 taps x 64 phases, tap k of phase p at
 * fir[k * 64 - p]); data: the sample buffer incl. its 8 guard samples; inc: per-sample increments (<< 17);
 * vel: per-sample velocity / envelope (int16, stock: velocity << 8) */
void dp_stock_note_on(dp_voice *v, const dp_pad *pad);
void dp_stock_note_off(dp_voice *v, const dp_pad *pad);
void dp_stock_render(dp_voice *v, int16_t *out, const dp_pad *pad, const int32_t *inc, const int16_t *vel,
                     const int16_t *data, const int16_t *fir);
/* shared with dp_plus.c: region bound of a pad's hi / lo words; one interpolated, gained output sample */
int32_t dp_bound(int16_t hi_w, int16_t lo_w);
int16_t dp_tap_sample(const int16_t *data, const int16_t *fir, int32_t pos, int32_t g);
/* the stock per-sample gain: vol^2 * e^2 (e = velocity / envelope, Q15-ish int16) */
static inline int32_t dp_gain(int32_t vol, int32_t e)
{
    int32_t g = (vol * e * 2 + 0x8000) >> 16; g = g > 32767 ? 32767 : g < -32768 ? -32768 : g;
    g = (g * vol * 2 + 0x8000) >> 16;         g = g > 32767 ? 32767 : g < -32768 ? -32768 : g;
    g = (g * e * 2 + 0x8000) >> 16;           return g > 32767 ? 32767 : g < -32768 ? -32768 : g;
}
#endif
