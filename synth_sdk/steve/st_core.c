/* steve core (docs/51): see st_core.h. Positions are an index plus a Q32 fraction (exact rates, 32-bit math per sample);
 * per block only: the steps logic (one 64-bit division when a copy lands on its unit). */
#include "st_core.h"

#define XFS     (ST_XF + 1)                                   /* captured past the loop end (crossfade + interpolation) */
#define XF_INV  ((1 << 30) / ST_XF)                           /* i * XF_INV >> 15 = i / ST_XF in Q15 */
#define RAMP    64                                            /* wet fade per sample (Q15): ~11 ms */

static const int16_t gain_n[ST_COPIES + 1] = { 0, 32767, 20316, 16384, 14418 };   /* per copy, by count */
static const int16_t pan_l[ST_COPIES] = { 32767, 0, 22938, 9830 }, pan_r[ST_COPIES] = { 0, 32767, 9830, 22938 };

static inline int16_t sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

void st_init(st_t *s, int16_t *buf, int32_t cap)
{
    uint8_t *b = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof *s; i++) b[i] = 0;
    s->buf = buf; s->cap = cap;
}

void st_unison(st_t *s)
{
    for (int k = 0; k < ST_COPIES; k++) { s->p[k] = s->p[0]; s->tgt[k] = 0; s->held[k] = 0; }
    s->loops = 0;
}

void st_capture(st_t *s, int32_t len, int32_t delay)
{
    if (!s->buf) return;
    if (len > s->cap - XFS) len = s->cap - XFS;
    if (len < 2 * ST_XF) len = 2 * ST_XF;
    s->want = len; s->wait = delay < 0 ? 0 : delay; s->state = ST_ARMED;
}

uint32_t st_offset(const st_t *s, int k)
{
    int32_t o = (int32_t)s->p[k].i - (int32_t)s->p[0].i;
    return (uint32_t)(o < 0 ? o + s->len : o);
}

static inline int32_t read_at(const int16_t *b, uint32_t i, uint32_t f)   /* linear interpolation */
{
    int32_t a = b[i];
    return a + (((b[i + 1] - a) * (int32_t)(f >> 17)) >> 15);
}

/* steps mode, per block: copy k's extra rate (Q32) for these n samples; lands exactly on its target */
static int32_t step_rate(st_t *s, const st_par *q, int k, int n)
{
    if (s->held[k] >= 0) return 0;                            /* holding: in step with A */
    uint32_t o = st_offset(s, k), r = (s->tgt[k] + (uint32_t)s->len - o) % (uint32_t)s->len;
    int64_t per_block = (int64_t)k * q->trans * n;            /* Q32 samples gained in this block at full speed */
    if (((int64_t)r << 32) > per_block) return k * q->trans;
    s->held[k] = 0;                                           /* lands in this block: the exact rest, then hold */
    return (int32_t)(((int64_t)r << 32) / n);
}

void st_render(st_t *s, const st_par *q, const int16_t *in_l, const int16_t *in_r, int16_t *out_l, int16_t *out_r, int n)
{
    int copies = q->copies < 2 ? 2 : q->copies > ST_COPIES ? ST_COPIES : q->copies;
    int32_t g = gain_n[copies], mix = q->mix < 0 ? 0 : q->mix > 32767 ? 32767 : q->mix;
    int playing = s->state == ST_PLAY || (s->state != ST_EMPTY && s->len);   /* the old loop fades out on a re-capture */
    int32_t want_wet = s->state == ST_PLAY ? 32767 : 0;
    uint32_t inc_i[ST_COPIES], inc_f[ST_COPIES];
    int32_t was_held[ST_COPIES];
    if (playing) {
        uint32_t unit = (uint32_t)s->len / (uint32_t)(q->div < 1 ? 1 : q->div);
        if (q->trans > 0 && !s->steps)                        /* entering steps: each copy heads for its next unit */
            for (int k = 1; k < ST_COPIES; k++) {
                uint32_t u = (uint32_t)k * unit, o = st_offset(s, k);
                s->tgt[k] = u ? (o + u - 1) / u * u % (uint32_t)s->len : 0;
                s->held[k] = -1;
            }
        s->steps = q->trans > 0;
        for (int k = 0; k < copies; k++) {
            was_held[k] = s->held[k];
            int32_t d = 0;
            if (k && q->trans > 0) {
                if (s->held[k] >= 0 && s->held[k] >= (q->hold < 1 ? 1 : q->hold)) {   /* held long enough: next unit */
                    s->tgt[k] = (s->tgt[k] + (uint32_t)k * unit) % (uint32_t)s->len;
                    s->held[k] = -1;
                }
                d = step_rate(s, q, k, n);
            } else if (k) d = k * q->drift;
            inc_i[k] = d < 0 ? 0 : 1;
            inc_f[k] = (uint32_t)d;                           /* 1 + d as index + Q32 fraction (d < 0: 0 + 2^32 + d) */
        }
    }
    for (int t = 0; t < n; t++) {
        int32_t xl = in_l[t], xr = in_r ? in_r[t] : xl, x = in_r ? (xl + xr) >> 1 : xl;
        if (s->state == ST_ARMED && s->wet == 0 && s->wait-- <= 0) { s->state = ST_CAPTURE; s->wp = 0; }
        if (s->state == ST_CAPTURE) {
            s->buf[s->wp++] = (int16_t)x;
            if (s->wp == s->want + XFS) {
                s->len = s->want; s->state = ST_PLAY; s->takes++;
                s->p[0].i = s->p[0].f = 0;
                st_unison(s);
                playing = 0;                                  /* rates of the new loop: next block */
            }
        }
        int32_t wl = 0, wr = 0;
        if (playing && s->wet) {
            for (int k = 0; k < copies; k++) {
                st_pos *p = &s->p[k];
                int32_t v = read_at(s->buf, p->i, p->f);
                if (p->i < ST_XF) {                           /* loop start: the tail (past the end) fades in to the head */
                    int32_t tail = read_at(s->buf, p->i + (uint32_t)s->len, p->f);
                    v = tail + (((v - tail) * (int32_t)((p->i * XF_INV) >> 15)) >> 15);
                }
                v = (v * g) >> 15;
                if (out_r) { wl += (v * pan_l[k]) >> 15; wr += (v * pan_r[k]) >> 15; } else wl += v;
            }
        }
        if (playing) {
            for (int k = 0; k < copies; k++) {
                st_pos *p = &s->p[k];
                uint32_t f = p->f + inc_f[k];
                p->i += inc_i[k] + (f < p->f);
                p->f = f;
                if (p->i >= (uint32_t)s->len) {
                    p->i -= (uint32_t)s->len;
                    if (!k) { s->loops++; for (int j = 1; j < copies; j++) if (s->held[j] >= 0) s->held[j]++; }
                }
            }
        }
        s->wet += s->wet < want_wet ? RAMP : s->wet > want_wet ? -RAMP : 0;
        if (s->wet < 0) s->wet = 0; else if (s->wet > 32767) s->wet = 32767;
        int32_t w = (mix * s->wet) >> 15;                     /* the dry gives way only as the loop fades in: no loop */
        if (out_r) {                                          /* (or mix 0) = the input untouched */
            out_l[t] = sat16(xl - ((xl * w) >> 15) + ((wl * w) >> 15));
            out_r[t] = sat16(xr - ((xr * w) >> 15) + ((wr * w) >> 15));
        } else out_l[t] = sat16(x - ((x * w) >> 15) + ((wl * w) >> 15));
    }
    if (playing && s->steps)                                   /* landed in this block: exactly on the unit */
        for (int k = 1; k < copies; k++)
            if (was_held[k] < 0 && s->held[k] >= 0) {
                uint32_t i = s->p[0].i + s->tgt[k];
                s->p[k].i = i >= (uint32_t)s->len ? i - (uint32_t)s->len : i;
                s->p[k].f = s->p[0].f;
            }
}
