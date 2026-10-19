/* steve: Steve Reich's phase process as an FX (docs/51). A captured loop plays as 2..4 copies that drift apart
 * (drift mode) or move one unit at a time and lock (steps mode). Pure fixed point, no division per sample; the
 * firmware glue (steve.c) maps knobs to st_par and the master's stereo out. */
#ifndef ST_CORE_H
#define ST_CORE_H
#include <stdint.h>

#define ST_COPIES 4
#define ST_XF     441                      /* wrap crossfade: 10 ms; the capture takes this much past the loop end */

enum { ST_EMPTY, ST_ARMED, ST_CAPTURE, ST_PLAY };

typedef struct {
    int32_t  drift;      /* drift mode: copy k runs at 1 + k * drift (Q32 fraction, signed: 0.004 * 2^32 = +0.4 %) */
    int32_t  trans;      /* 0: drift mode; > 0: steps mode, copy k moves at 1 + k * trans (Q32) towards its next unit */
    int32_t  div;        /* steps: units per loop (4 .. 16) */
    int32_t  hold;       /* steps: loops held on each unit (1 .. 16) */
    int32_t  copies;     /* 2 .. 4 */
    int32_t  mix;        /* wet, Q15 0 .. 32767 */
} st_par;

typedef struct { uint32_t i, f; } st_pos;     /* sample index + Q32 fraction */

typedef struct {
    int16_t *buf;        /* mono loop (capture = mono sum), len + ST_XF samples */
    int32_t  cap;        /* buffer size, samples */
    int32_t  len;        /* loop length (samples) */
    int32_t  state, wait, wp, want;   /* capture: samples to wait (beat), write position, length asked */
    st_pos   p[ST_COPIES];
    uint32_t tgt[ST_COPIES];          /* steps: offset (samples) copy k is heading for / holding */
    int32_t  held[ST_COPIES];         /* steps: loops held so far on tgt (-1: moving) */
    uint32_t loops;                   /* A's wraps since unison */
    int32_t  wet;                     /* wet gain ramp Q15 (fades around a capture) */
    int32_t  steps;                   /* steps mode was on in the last block (entering it sets the targets) */
    uint32_t takes;                   /* captures finished (the screen redraws the waveform when it changes) */
} st_t;

void st_init(st_t *s, int16_t *buf, int32_t cap);
/* capture `len` samples (capped to the buffer) starting `delay` samples into the next blocks (beat sync; 0: now) */
void st_capture(st_t *s, int32_t len, int32_t delay);
void st_unison(st_t *s);                         /* every copy back on A */
/* n samples: in_l / in_r the input (in_r 0: mono), out_l / out_r (out_r 0: mono out into out_l); in and out may be
 * the same buffers. Stereo: the dry stays stereo, the capture takes the mono sum, the copies are panned. */
void st_render(st_t *s, const st_par *q, const int16_t *in_l, const int16_t *in_r, int16_t *out_l, int16_t *out_r, int n);
uint32_t st_offset(const st_t *s, int k);        /* copy k's position minus A's, samples 0 .. len-1 */

#endif
