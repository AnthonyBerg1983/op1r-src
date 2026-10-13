/* Paulstretch core, fixed point (see paulstretch_dsp.h).
 *
 * Frame job, M = N/2 complex points, L = log2 M, buffer = z[n] = x[2n] + i x[2n+1] packed as two int16 per
 * word (re low, im high), so the FFT runs on the Blackfin's 16-bit complex vector ops (builtins, emulated on the
 * host): one butterfly ~11 instructions instead of ~45 with 32-bit multiplies, half the memory.
 *   GATHER   windowed source -> z in bit-reversed order, Q14 (|z| <= 1 in Q15)
 *   FFWD     L radix-2 DIT stages, block floating point: a stage shifts >> 1 only when the previous stage's peak
 *            needs it (counted in ef); keeps |z| < 1, quiet frames keep their precision
 *   MAGS     split Z into the real spectrum X[k] = E + W^k O -> magnitudes (alpha-max-beta-min, int32)
 *   SYNTH    Y[k] = |X[k / pitch]| normalised by 2^-ms (peak 8191 so the merged spectrum fits int16), random
 *            phase, merged into the half-size spectrum Z'[k] = E' + i O' for the inverse
 *   BITREV   in-place bit-reverse permutation of Z'
 *   FINV     L stages, block floating point (ei)
 *   OUT      x = z * 2^(ef + ms + ei - L + 1) (absolute Q15 level), window, overlap-add with the last tail
 */
#include "paulstretch_dsp.h"
#include "paulstretch_tables.h"

enum { GATHER, FFWD, MAGS, SYNTH, BITREV, FINV, OUT, DONE };
volatile uint32_t ps_prof[7];
#define ALPHA 31470                                    /* |z| ~ 0.96043 max + 0.39782 min (max error 4 %) */
#define BETA  13036
#define NOSHIFT_MAX 11585                              /* stage input peak (per component) that may grow x2 */
#define MAG_NORM 16383                                 /* peak magnitude fed to the synthesis (Z' <= 2x) */

/* ---- packed complex int16 (re = low half, im = high half) ---- */
#ifdef __bfin__
typedef short v2 __attribute__((vector_size(4)));
static inline v2 cmul(v2 a, v2 b) { return __builtin_bfin_cmplx_mul(a, b); }   /* fractional, rounded, sat */
static inline v2 vadd(v2 a, v2 b) { return __builtin_bfin_add_fr2x16(a, b); }
static inline v2 vsub(v2 a, v2 b) { return __builtin_bfin_sub_fr2x16(a, b); }
static inline v2 vhalf(v2 a) { return __builtin_bfin_shl_fr2x16(a, -1); }
static inline v2 vabs(v2 a) { return __builtin_bfin_abs_fr2x16(a); }
static inline v2 vmax(v2 a, v2 b) { return __builtin_bfin_max_fr2x16(a, b); }
static inline v2 pk(int32_t re, int32_t im) { v2 r = { (short)re, (short)im }; return r; }
static inline int32_t re_of(v2 a) { return a[0]; }
static inline int32_t im_of(v2 a) { return a[1]; }
static inline uint32_t cycles(void) { uint32_t c; __asm__ volatile ("%0 = CYCLES;" : "=d"(c)); return c; }
#else
typedef struct { int16_t r, i; } v2;                    /* host emulation of the Blackfin semantics */
static inline int16_t s16(int64_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }
static inline v2 cmul(v2 a, v2 b)
{
    int64_t re = 2 * ((int64_t)a.r * b.r - (int64_t)a.i * b.i), im = 2 * ((int64_t)a.r * b.i + (int64_t)a.i * b.r);
    v2 o = { s16((re + 0x8000) >> 16), s16((im + 0x8000) >> 16) }; return o;
}
static inline v2 vadd(v2 a, v2 b) { v2 o = { s16(a.r + b.r), s16(a.i + b.i) }; return o; }
static inline v2 vsub(v2 a, v2 b) { v2 o = { s16(a.r - b.r), s16(a.i - b.i) }; return o; }
static inline v2 vhalf(v2 a) { v2 o = { (int16_t)(a.r >> 1), (int16_t)(a.i >> 1) }; return o; }
static inline v2 vabs(v2 a) { v2 o = { s16(a.r < 0 ? -(int32_t)a.r : a.r), s16(a.i < 0 ? -(int32_t)a.i : a.i) }; return o; }
static inline v2 vmax(v2 a, v2 b) { v2 o = { a.r > b.r ? a.r : b.r, a.i > b.i ? a.i : b.i }; return o; }
static inline v2 pk(int32_t re, int32_t im) { v2 r = { (int16_t)re, (int16_t)im }; return r; }
static inline int32_t re_of(v2 a) { return a.r; }
static inline int32_t im_of(v2 a) { return a.i; }
static inline uint32_t cycles(void) { return 0; }
#endif
#define Z(p) ((v2 *)(p)->buf)

#define M16(a, b) ((int32_t)(int16_t)(a) * (int16_t)(b))   /* 16 x 16 -> 32 (one MAC on the Blackfin) */
/* (a * w) >> 15 exactly, 32 x 16 bit without a 64-bit multiply */
static inline int32_t mulw(int32_t a, int32_t w) { return (a >> 15) * w + (((a & 0x7FFF) * w) >> 15); }
static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline int32_t shl(int32_t v, int s) { return s >= 0 ? v * (1 << s) : v >> -s; }

static inline int32_t sinq(uint32_t i)                 /* sin(2 pi i / PS_NMAX), Q15 */
{
    i &= PS_NMAX - 1;
    uint32_t q = i / (PS_NMAX / 4), r = i % (PS_NMAX / 4);
    switch (q) {
    case 0:  return ps_sin_q[r];
    case 1:  return ps_sin_q[PS_NMAX / 4 - r];
    case 2:  return -ps_sin_q[r];
    default: return -ps_sin_q[PS_NMAX / 4 - r];
    }
}
static inline int32_t cosq(uint32_t i) { return sinq(i + PS_NMAX / 4); }

static uint8_t rev8[256];
static inline uint32_t bitrev(uint32_t i, int bits) { return ((uint32_t)rev8[i & 255] << 8 | rev8[(i >> 8) & 255]) >> (16 - bits); }

static inline int32_t win(const ps_t *p, int n)         /* window value of sample n of an N frame, Q15 */
{
    int s = PS_NMAX >> p->log2n, m = n * s + (s >> 1);
    return m < PS_NMAX / 2 ? ps_win[m] : ps_win[PS_NMAX - 1 - m];
}

void ps_init(ps_t *p, void *mem, int log2n, uint32_t seed)
{
    for (int i = 0; i < 256; i++) {
        int r = 0;
        for (int b = 0; b < 8; b++) if (i & (1 << b)) r |= 0x80 >> b;
        rev8[i] = (uint8_t)r;
    }
    p->log2n = log2n; p->n = 1 << log2n; p->h = p->n >> 1;
    p->buf = (uint32_t *)mem;                              /* M packed complex */
    p->out[0] = (int16_t *)(p->buf + p->h);
    p->out[1] = p->out[0] + p->h;
    p->tail = (int32_t *)(p->out[1] + p->h);
    p->mag = p->tail + p->h;
    for (int i = 0; i < p->h; i++) { p->out[0][i] = p->out[1][i] = 0; p->tail[i] = 0; }
    p->src = 0; p->src_len = 0; p->pos = 0; p->pos_frac = 0;
    p->play = 0; p->rd = 0; p->ready = 0; p->rnd = seed | 1;
    p->phase = GATHER; p->s = p->j = p->g = p->k = 0; p->work = 0;
    p->pinv_q16 = 65536; p->jitter = 0;
    ps_set_stretch(p, 256 * 8);
}

void ps_set_pitch_inv(ps_t *p, uint32_t r)
{
    p->pinv_q16 = r < 8192 ? 8192 : r > 524288 ? 524288 : r;   /* k * pinv < 2^31 for k <= 4096 */
}

void ps_set_source(ps_t *p, const int16_t *src, int32_t len, int32_t pos)
{
    p->src = src; p->src_len = len; p->pos = len > 0 ? pos % len : 0; p->pos_frac = 0;
}

void ps_set_stretch(ps_t *p, uint32_t st)
{
    if (st < 64) st = 64;                                /* >= 0.25x */
    uint32_t num = (uint32_t)p->h << 16, q = num / st, r = num % st;   /* adv = H / stretch, Q16 */
    p->adv_q16 = (q << 8) + (r << 8) / st;
}

int ps_units_per_frame(const ps_t *p)
{
    int m = p->h, l = p->log2n - 1;
    return m + 2 * l * (m >> 1) + 5 * (m >> 1) + m + m;  /* gather, 2 FFTs, mags + synth, bitrev, out */
}

/* ---- phases (each consumes budget units, returns the budget left) ---- */
static int ph_gather(ps_t *p, int b)
{
    int m = p->h, bits = p->log2n - 1;
    int32_t len = p->src_len;
    if (p->k == 0) {                                      /* frame start: position + random smear */
        p->fstart = p->pos;
        if (p->jitter > 0) { p->rnd = p->rnd * 1664525u + 1013904223u; p->fstart += (int32_t)((p->rnd >> 16) * (uint32_t)p->jitter >> 16); }
        if (len > 0) while (p->fstart >= len) p->fstart -= len;
        p->ef = p->ei = 0; p->vmax = 0;
    }
    v2 *z = Z(p);
    while (b > 0 && p->k < m) {
        int n = 2 * p->k;
        int32_t i0 = p->fstart + n;
        if (i0 >= len) i0 -= len;                         /* loop the source (len >= N) */
        int32_t i1 = i0 + 1 >= len ? 0 : i0 + 1;
        int32_t x0 = len ? p->src[i0] : 0, x1 = len ? p->src[i1] : 0;
        int32_t a = (x0 * win(p, n)) >> 16, c = (x1 * win(p, n + 1)) >> 16;   /* Q14 */
        z[bitrev(p->k, bits)] = pk(a, c);
        a = a < 0 ? -a : a; c = c < 0 ? -c : c;
        if (a > p->vmax) p->vmax = a;
        if (c > p->vmax) p->vmax = c;
        p->k++; b--;
    }
    return b;
}

static int ph_fft(ps_t *p, int b, int inverse)          /* stage s, column j, group g; block floating point */
{
    int m = p->h, l = p->log2n - 1;
    v2 *z = Z(p);
    while (b > 0 && p->s < l) {
        if (p->j == 0 && p->g == 0) {                     /* stage start: decide the shift from the last peak */
            p->shift = p->vmax > NOSHIFT_MAX;             /* vmax: GATHER / SYNTH / previous stage peak */
            if (p->shift) { if (inverse) p->ei++; else p->ef++; }
            p->vmax = 0;
        }
        int h = 1 << p->s, g = p->g;
        uint32_t ti = (uint32_t)p->j * (PS_NMAX >> (p->s + 1));   /* angle 2 pi j / 2h */
        v2 w = pk(cosq(ti), inverse ? sinq(ti) : -sinq(ti));
        if (re_of(w) == 32767 && im_of(w) == 0) w = pk(32767, 0);
        int n = (m - g + 2 * h - 1) >> (p->s + 1);        /* butterflies left in this column */
        if (n > b) n = b;
        v2 *a = z + g + p->j, *c = a + h, mx = pk(0, 0);
        int step = 2 * h;
        b -= n; g += 2 * h * n;
        if (p->shift)
            for (; n > 0; n--, a += step, c += step) {
                v2 t = vhalf(cmul(*c, w)), x = vhalf(*a), u = vadd(x, t), v = vsub(x, t);
                *a = u; *c = v; mx = vmax(mx, vmax(vabs(u), vabs(v)));
            }
        else
            for (; n > 0; n--, a += step, c += step) {
                v2 t = cmul(*c, w), x = *a, u = vadd(x, t), v = vsub(x, t);
                *a = u; *c = v; mx = vmax(mx, vmax(vabs(u), vabs(v)));
            }
        int32_t m1 = re_of(mx) > im_of(mx) ? re_of(mx) : im_of(mx);
        if (m1 > p->vmax) p->vmax = m1;
        p->g = g;
        if (p->g >= m) { p->g = 0; if (++p->j == h) { p->j = 0; p->s++; } }
    }
    return b;
}

/* X[k] from Z[k], Z[M-k]: E = (Zk + conj Zm) / 2, O = (Zk - conj Zm) / 2i, X = E + W^k O, W = e^(-2 pi i / N) */
static void split(const ps_t *p, int k, v2 zk, v2 zm, int32_t *xr, int32_t *xi)
{
    int32_t er = (re_of(zk) + re_of(zm)) >> 1, ei = (im_of(zk) - im_of(zm)) >> 1;
    int32_t or_ = (im_of(zk) + im_of(zm)) >> 1, oi = -((re_of(zk) - re_of(zm)) >> 1);
    uint32_t ti = (uint32_t)k << (14 - p->log2n);         /* angle 2 pi k / N in PS_NMAX units */
    int32_t wr = cosq(ti), wi = -sinq(ti);
    *xr = er + ((M16(or_, wr) - M16(oi, wi)) >> 15);
    *xi = ei + ((M16(or_, wi) + M16(oi, wr)) >> 15);
}
/* Z'[k] from Y[k], Y[M-k]: E = (Yk + conj Ym) / 2, O = (Yk - conj Ym) W^-k / 2, Z' = E + i O */
static v2 merge(const ps_t *p, int k, int32_t yr, int32_t yi, int32_t mr, int32_t mi)
{
    int32_t er = (yr + mr) >> 1, ei = (yi - mi) >> 1, dr = (yr - mr) >> 1, di = (yi + mi) >> 1;
    uint32_t ti = (uint32_t)k << (14 - p->log2n);
    int32_t wr = cosq(ti), wi = sinq(ti);                  /* W^-k */
    int32_t or_ = (M16(dr, wr) - M16(di, wi)) >> 15, oi = (M16(dr, wi) + M16(di, wr)) >> 15;
    return pk(sat16(er - oi), sat16(ei + or_));
}
static inline int32_t magnitude(int32_t re, int32_t im)
{
    int32_t ar = re < 0 ? -re : re, ai = im < 0 ? -im : im;
    return ar > ai ? (ar * ALPHA + ai * BETA) >> 15 : (ai * ALPHA + ar * BETA) >> 15;
}
static inline int32_t mag_at(const ps_t *p, int k)       /* |X| at bin k * pinv (linear between bins), normalised */
{
    uint32_t q = (uint32_t)k * p->pinv_q16, i = q >> 16;
    if (i >= (uint32_t)p->h) return 0;
    int32_t a = p->mag[i], b = p->mag[i + 1];
    int32_t v = shl(a + mulw(b - a, (int32_t)(q & 0xFFFF) >> 1), -p->ms);
    return v > MAG_NORM ? MAG_NORM : v;                   /* bin M (not in the peak) / rounding */
}
static void phase(ps_t *p, int32_t mag, int32_t *re, int32_t *im)   /* random phase */
{
    p->rnd = p->rnd * 1664525u + 1013904223u;
    uint32_t ph = p->rnd >> (32 - 14);
    *re = M16(mag, cosq(ph)) >> 15; *im = M16(mag, sinq(ph)) >> 15;    /* mag <= MAG_NORM */
}

static int32_t *test_re, *test_im;                      /* host test: capture X / inject Y */
static const int32_t *test_yr, *test_yi;

static int ph_mags(ps_t *p, int b)                      /* X[k], X[M-k] -> magnitudes, peak */
{
    int m = p->h;
    v2 *z = Z(p);
    if (p->k == 0) p->vmax = 0;
    while (b > 0 && p->k <= (m >> 1)) {
        int k = p->k, km = (m - k) & (m - 1);
        int32_t xr, xi, yr, yi;
        split(p, k, z[k], z[km], &xr, &xi);
        split(p, m - k, z[km], z[k], &yr, &yi);
        if (test_re) { test_re[k] = xr; test_im[k] = xi; test_re[m - k] = yr; test_im[m - k] = yi; }
        int32_t a = magnitude(xr, xi), c = magnitude(yr, yi);
        p->mag[k] = a; p->mag[m - k] = c;
        if (k && a > p->vmax) p->vmax = a;
        if (k && c > p->vmax && km != k) p->vmax = c;
        p->k++; b -= 2;
    }
    if (p->k > (m >> 1)) {                                /* normalisation for the synthesis: peak -> ~MAG_NORM */
        int s = 0; int32_t v = p->vmax;
        if (v > 0) { while (v > MAG_NORM) { v >>= 1; s++; } while (v <= MAG_NORM / 2 && s > -15) { v <<= 1; s--; } }
        p->ms = s;
    }
    return b;
}

static int ph_synth(ps_t *p, int b)                     /* pitch-shifted magnitudes, random phases -> Z' */
{
    int m = p->h;
    v2 *z = Z(p);
    if (p->k == 0) p->vmax = 0;
    while (b > 0 && p->k <= (m >> 1)) {
        int k = p->k, km = (m - k) & (m - 1);
        int32_t xr, xi, yr, yi;                           /* Y[k], Y[M-k] */
        if (test_yr) { xr = test_yr[k]; xi = test_yi[k]; yr = test_yr[m - k]; yi = test_yi[m - k]; }
        else if (k == 0) { xr = xi = yr = yi = 0; }       /* DC and Nyquist off */
        else {
            phase(p, mag_at(p, k), &xr, &xi);
            if (km != k) phase(p, mag_at(p, m - k), &yr, &yi); else { yr = xr; yi = xi; }
        }
        v2 u = merge(p, k, xr, xi, yr, yi), w = merge(p, m - k, yr, yi, xr, xi);
        z[k] = u;
        if (km != k) z[km] = w;
        v2 a = vmax(vabs(u), vabs(w));
        int32_t m1 = re_of(a) > im_of(a) ? re_of(a) : im_of(a);
        if (m1 > p->vmax) p->vmax = m1;
        p->k++; b -= 3;
    }
    return b;
}

static int ph_bitrev(ps_t *p, int b)
{
    int m = p->h, bits = p->log2n - 1;
    uint32_t *z = p->buf;
    while (b > 0 && p->k < m) {
        uint32_t r = bitrev(p->k, bits);
        if (r > (uint32_t)p->k) { uint32_t t = z[r]; z[r] = z[p->k]; z[p->k] = t; }
        p->k++; b--;
    }
    return b;
}

static int ph_out(ps_t *p, int b)                       /* sample pair k: n = 2k, 2k+1 */
{
    int m = p->h, hh = p->h, sh = p->ef + p->ms + p->ei - (p->log2n - 1) + 1;
    int16_t *o = p->out[p->play ^ 1];
    v2 *z = Z(p);
    while (b > 0 && p->k < m) {
        v2 zz = z[p->k];
        for (int e = 0; e < 2; e++) {
            int n = 2 * p->k + e;
            int32_t v = mulw(shl(e ? im_of(zz) : re_of(zz), sh), win(p, n));   /* Q15 */
            if (n < hh) o[n] = (int16_t)sat16(p->tail[n] + v);
            else p->tail[n - hh] = v;
        }
        p->k++; b--;
    }
    return b;
}

int ps_work(ps_t *p, int units)
{
    int b = units;
    while (b > 0 && p->phase != DONE) {
        int left, ph = p->phase;
        uint32_t t0 = cycles();
        switch (ph) {
        case GATHER: left = ph_gather(p, b); break;
        case FFWD:   left = ph_fft(p, b, 0); break;
        case MAGS:   left = ph_mags(p, b); break;
        case SYNTH:  left = ph_synth(p, b); break;
        case BITREV: left = ph_bitrev(p, b); break;
        case FINV:   left = ph_fft(p, b, 1); break;
        default:     left = ph_out(p, b); break;
        }
        ps_prof[ph] += cycles() - t0;
        p->work += (uint32_t)(b - (left > 0 ? left : 0));
        b = left;
        int fin = (ph == GATHER || ph == BITREV || ph == OUT) ? p->k >= p->h
                : (ph == MAGS || ph == SYNTH) ? p->k > (p->h >> 1) : p->s >= p->log2n - 1;
        if (fin) { p->phase++; p->k = p->s = p->j = p->g = 0; }
    }
    if (p->phase == DONE) p->ready = 1;
    return p->ready;
}

static void next_frame(ps_t *p)                         /* pending frame becomes the playing one */
{
    p->play ^= 1; p->rd = 0; p->ready = 0;
    uint32_t f = p->pos_frac + p->adv_q16;
    p->pos += (int32_t)(f >> 16); p->pos_frac = f & 0xFFFF;
    if (p->src_len > 0) while (p->pos >= p->src_len) p->pos -= p->src_len;
    p->phase = GATHER; p->k = p->s = p->j = p->g = 0; p->work = 0;
}

void ps_render(ps_t *p, int16_t *out, int n)
{
    /* spread: finish a frame within 3/4 of a hop -> units per call = upf * n / (0.75 H) */
    int units = (int)(((uint32_t)ps_units_per_frame(p) * (uint32_t)n * 4u / 3u) >> (p->log2n - 1)) + 1;
    if (!p->ready) ps_work(p, units);
    for (int i = 0; i < n; i++) {
        if (p->rd >= p->h) {
            if (!p->ready) ps_work(p, 0x7FFFFFFF);         /* fallback: should not happen with the budget above */
            next_frame(p);
            ps_work(p, units);                             /* keep the spread going right away */
        }
        out[i] = p->out[p->play][p->rd++];
    }
}

#ifndef __bfin__
/* ---- host test helpers (old fixed scaling: X = 2 X_true / N for a Q23 input, see paulstretch_compare.py) ---- */
void ps_test_rfft(ps_t *p, const int16_t *x, int32_t *re, int32_t *im)
{
    const int16_t *s = p->src; int32_t l = p->src_len, pos = p->pos;
    p->src = x; p->src_len = p->n; p->pos = 0; p->jitter = 0;
    p->phase = GATHER; p->k = p->s = p->j = p->g = 0;
    test_re = re; test_im = im;
    ph_gather(p, 1 << 20); p->k = 0;
    ph_fft(p, 1 << 20, 0); p->s = p->j = p->g = 0;
    ph_mags(p, 1 << 20);
    test_re = test_im = 0;
    int sh = 9 + p->ef - (p->log2n - 1);                  /* Q14 in, 2^-ef  ->  Q23 in, 2^-L */
    for (int k = 0; k <= p->h; k++) { re[k] = shl(re[k], sh); im[k] = shl(im[k], sh); }
    p->src = s; p->src_len = l; p->pos = pos; p->phase = GATHER; p->k = 0; p->ready = 0;
}
void ps_test_irfft(ps_t *p, const int32_t *re, const int32_t *im, int32_t *x)
{
    static int32_t yr[PS_NMAX / 2 + 1], yi[PS_NMAX / 2 + 1];
    int32_t mx = 1; int s = 0;
    for (int k = 0; k <= p->h; k++) { int32_t a = re[k] < 0 ? -re[k] : re[k], c = im[k] < 0 ? -im[k] : im[k]; if (a > mx) mx = a; if (c > mx) mx = c; }
    while (mx > MAG_NORM / 2) { mx >>= 1; s++; }
    for (int k = 0; k <= p->h; k++) { yr[k] = re[k] >> s; yi[k] = im[k] >> s; }
    p->phase = SYNTH; p->k = p->s = p->j = p->g = 0; p->ei = 0;
    test_yr = yr; test_yi = yi;
    ph_synth(p, 1 << 20);
    test_yr = test_yi = 0;
    p->k = 0; ph_bitrev(p, 1 << 20);
    p->k = p->s = p->j = p->g = 0; ph_fft(p, 1 << 20, 1);
    v2 *z = Z(p);
    for (int i = 0; i < p->h; i++) { x[2 * i] = shl(re_of(z[i]), s + p->ei); x[2 * i + 1] = shl(im_of(z[i]), s + p->ei); }
    p->phase = GATHER; p->ready = 0; p->k = p->s = p->j = p->g = 0;
}
#endif
