/* Paulstretch core (fixed point, no float, no libc): extreme time stretch of a mono int16 source.
 * Algorithm (Paul Nasca, public domain description): window N samples of the source, FFT, keep the magnitudes,
 * random phases, inverse FFT, window again, overlap-add with hop H = N/2. The source position advances by
 * H / stretch per hop.
 *
 * Built for the OP-1 audio callback: one frame (= H output samples) is computed in small slices spread over the
 * blocks that play the previous frame (ps_render does a fixed amount of work per call), so there is no burst.
 * Latency: one frame. Memory: caller supplies PS_MEM_BYTES(log2n) bytes (4-aligned).
 */
#ifndef PAULSTRETCH_DSP_H
#define PAULSTRETCH_DSP_H
#include <stdint.h>

#define PS_LOG2N_MIN 10
#define PS_LOG2N_MAX 14                                   /* N = 1024 .. 16384 */
/* packed complex int16 FFT buffer [N/2] + output double buffer int16[2][H] + overlap tail int32[H] + magnitudes int32[H+1] */
#define PS_MEM_BYTES(log2n) ((2u << (log2n)) + (2u << (log2n)) + (2u << (log2n)) + (2u << (log2n)) + 4u)

typedef struct {
    int      log2n, n, h;                 /* frame N, hop H = N/2 */
    uint32_t *buf;                        /* N/2 complex, packed int16 (re low, im high) */
    int16_t *out[2];                      /* play / pending frame (H samples each) */
    int32_t *tail;                        /* second half of the last frame (H) */
    const int16_t *src; int32_t src_len;  /* source sample */
    int32_t  pos;                         /* source position (samples) of the next frame start */
    uint32_t pos_frac;                    /* .. fraction (Q16) */
    uint32_t adv_q16;                     /* source advance per hop, Q16 samples (H / stretch) */
    int32_t *mag;                         /* magnitudes of the current frame (H + 1) */
    uint32_t pinv_q16;                    /* pitch: 1 / ratio, Q16 (Y[k] = |X[k * pinv]|), 65536 = original */
    int32_t  jitter;                      /* random frame start offset 0..jitter samples (smear) */
    int32_t  fstart;                      /* start of the frame being gathered */
    int      play, rd;                    /* playing buffer index, read position in it */
    int      ready;                       /* pending frame complete */
    uint32_t rnd;                         /* phase LCG */
    int      ef, ei, ms, shift;           /* block floating point: forward / inverse shifts, magnitude norm, stage */
    int32_t  vmax;                        /* peak of the last stage / of the magnitudes */
    /* job state machine (one frame) */
    int      phase, s, j, g, k;
    uint32_t work;                        /* work units done in the current job (statistics) */
} ps_t;

void ps_init(ps_t *p, void *mem, int log2n, uint32_t seed);
void ps_set_source(ps_t *p, const int16_t *src, int32_t len, int32_t pos);
/* pitch as 1 / ratio, Q16 (65536 = original, 32768 = one octave up), spectral: bins move, no aliasing;
 * range 1/8 .. 8 (3 octaves down) */
void ps_set_pitch_inv(ps_t *p, uint32_t pinv_q16);
/* per-phase cycle counters (Blackfin CYCLES; 0 on the host): gather, fft, mags, synth, bitrev, ifft, out */
extern volatile uint32_t ps_prof[7];
/* stretch factor as Q8 (256 = 1x, 25600 = 100x); one division, call on parameter change only */
void ps_set_stretch(ps_t *p, uint32_t stretch_q8);
/* do up to `units` units of frame work (1 unit ~ 1 radix-2 butterfly), returns 1 when the pending frame is done */
int  ps_work(ps_t *p, int units);
/* units per output sample needed to finish a frame within one hop (for a budget per block) */
int  ps_units_per_frame(const ps_t *p);
/* render n output samples (Q15); does n * units_per_sample of work first */
void ps_render(ps_t *p, int16_t *out, int n);

/* exposed for the host test: forward real FFT of N samples (Q15 in) -> X[0..N/2] (re, im), scaled 1/N, Q22 */
void ps_test_rfft(ps_t *p, const int16_t *x, int32_t *re, int32_t *im);
/* inverse: X[0..N/2] -> N samples, as used by the frame job (before windowing), same internal scaling */
void ps_test_irfft(ps_t *p, const int32_t *re, const int32_t *im, int32_t *x);
#endif
