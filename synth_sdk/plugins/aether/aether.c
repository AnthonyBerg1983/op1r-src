/* "aether" - a tilt theremin (docs/36). The key sets the note; tilting the OP-1 plays it like the theremin's
 * antennas: forward / back bends the pitch, raising the right side fades the volume out, shaking adds vibrato.
 * The angle at note-on is "home" (play at any angle). Mono / legato / portamento: the stock play modes.
 *   knob 0 (blue)  : pitch range at full tilt, 1 .. 24 semitones
 *   knob 1 (green) : timbre, sine .. reedy (even harmonics + soft drive)
 *   knob 2 (white) : vibrato from shaking, 0 .. +-0.5 semitone
 *   knob 3 (red)   : volume tilt, 0 (off) .. full (silent at 30 degrees)
 *   blue press     : page 2 = dead zone around home, 0 .. 10 degrees   (word 5; unset = 8192 = 2.5 degrees)
 *   green press    : page 2 = tilt for the full range, 20 .. 60 degrees (word 6; unset = 30 degrees)
 *   white press    : page 2 = vibrato rate, 3 .. 8 Hz                  (word 7; unset = 4.25 Hz)
 * Sensor (docs/36): ADXL345, firmware copy at 0xFF801054 (X left side up +, Y front edge up -, Z up +), 1 g = 16384,
 * updated ~69 Hz. Tilt is used as the gravity component itself (sin of the angle), no atan: the home offset removes
 * the sensor offsets. Per block: smoothing (~30 ms), tilt -> pitch / gain, shake detector (change of the tilt
 * velocity), vibrato LFO, exp2; per sample: phase increment x multiplier (ramped across the block), sine, shaping.
 * A few divisions per block, none per sample. State per voice in our own array (voice memory is shared). */
#include "op1_plugin.h"

#ifndef OP1_HOST
#define AE_ACCEL ((const volatile int16_t *)0xFF801054)
#else
extern int16_t host_accel[3];
#define AE_ACCEL host_accel
#endif

typedef struct {
    uint32_t ph, vph;                 /* oscillator, vibrato LFO phase */
    int32_t sm[3], prev[3], slow[3];  /* smoothed vector (raw << 8), previous, slow average of its velocity */
    int32_t env;                      /* shake envelope, Q15 */
    int32_t gain, mult;               /* gain Q15, pitch multiplier Q14 (end of the last block) */
    int32_t semis;                    /* pitch bend incl. vibrato, semitones Q16 (screen) */
    int16_t hx, hy;                   /* home (raw) */
} ae_t;
static ae_t st[OP1_VOICES];
static volatile int ae_cur;           /* voice of the last note-on (screen) */

#define A_SM    3016                  /* 1 - exp(-128 / 44100 / 0.03)  Q15 */
#define A_SLOW  629                   /* 1 - exp(-128 / 44100 / 0.15)  Q15 */
#define SHAKE0  (48 << 8)             /* sum of |velocity change| per block, raw << 8: below = still (noise ~35) */
#define SHAKE_S 95                    /* ... full shake 95 raw per block above that (2.5 g/s) */
#define VOL_FULL 8192                 /* sin 30 deg: right side raised this far = silent at full red */
#define LEVEL   22000                 /* output level Q15 (matched to the other engines by ear) */

static inline int32_t page_word(const int16_t *p, int i) { return p[i] > 0 ? p[i] : 8192; }
static inline int32_t sin_deg_q10(int32_t deg_q10)  /* sin of an angle in degrees (Q10) -> raw units (1 g = 16384) */
{
    return OP1_SINE_TABLE[(deg_q10 * 8192 / 360 >> 10) & 8191] >> 1;
}
static inline int32_t iabs(int32_t a) { return a < 0 ? -a : a; }

/* 2^(x / 65536) for x in Q16 octaves, -2 .. +2 -> Q14 */
static int32_t exp2_q14(int32_t x)
{
    int32_t o = x >> 16, f = x & 0xFFFF;                     /* floor octave, fraction Q16 */
    f >>= 1;                                                 /* Q15 */
    /* 2^f ~ 1 + f (0.69515 + f (0.22568 + f 0.07917)); with the Q14 result < 1 cent (test) */
    int32_t p = 2594;                                        /* 0.07917 Q15 */
    p = 7395 + (p * f >> 15);
    p = 22779 + (p * f >> 15);
    p = 32768 + (p * f >> 15);                               /* Q15 */
    p >>= 1;                                                 /* Q14 */
    return o >= 0 ? p << o : p >> -o;
}

static void ae_note_on(void *self, const int16_t *params)
{
    (void)params;
    ae_cur = op1_voice_index(self) % OP1_VOICES;
    ae_t *s = &st[ae_cur];
    for (int i = 0; i < 3; i++) { s->sm[i] = s->prev[i] = AE_ACCEL[i] * 256; s->slow[i] = 0; }
    s->hx = AE_ACCEL[0]; s->hy = AE_ACCEL[1];
    s->ph = 0; s->env = 0; s->gain = 32767; s->mult = 16384;
}

static void ae_render(void *self, int16_t *out, const int16_t *params)
{
    ae_t *s = &st[op1_voice_index(self) % OP1_VOICES];
    /* ---- per block: sensor ---- */
    int32_t hp = 0;
    for (int i = 0; i < 3; i++) {
        s->sm[i] += (int32_t)(((int64_t)((AE_ACCEL[i] * 256) - s->sm[i]) * A_SM) >> 15);
        int32_t vel = s->sm[i] - s->prev[i]; s->prev[i] = s->sm[i];
        s->slow[i] += (int32_t)(((int64_t)(vel - s->slow[i]) * A_SLOW) >> 15);
        hp += iabs(vel - s->slow[i]);
    }
    int32_t m = hp > SHAKE0 ? (hp - SHAKE0) / SHAKE_S : 0;     /* Q8 (256 = full) */
    m = m > 256 ? 32767 : m << 7;
    s->env += m > s->env ? (m - s->env) * 10 >> 5 : -(s->env >> 8) - (s->env > 0);   /* fast attack, ~1 s release */

    /* pitch: forward tilt (front edge up = Y down) relative to home, dead zone, linear in sin(angle) */
    int32_t dead = sin_deg_q10(page_word(params, 5) * 10 * 1024 / 32767);
    int32_t full = sin_deg_q10((20 * 1024) + page_word(params, 6) * 40 * 1024 / 32767);
    if (full <= dead + 64) full = dead + 64;
    int32_t d = s->hy - (s->sm[1] >> 8);
    int32_t a = iabs(d) - dead; if (a < 0) a = 0;
    int32_t range_q8 = 256 + (params[0] * 23 >> 7);           /* semitones Q8, 1 .. 24 */
    int32_t t = (int32_t)(((int64_t)a << 15) / (full - dead)); if (t > 32767) t = 32767;   /* 0..1 Q15 */
    int32_t semis = (int32_t)((int64_t)range_q8 * t >> 7);    /* Q16 */
    if (d < 0) semis = -semis;
    /* vibrato */
    int32_t rate_mhz = 3000 + page_word(params, 7) * 5000 / 32767;
    s->vph += (uint32_t)rate_mhz * 12466u;                     /* 2^32 * Hz * 128 / 44100 / 1000 */
    int32_t depth = (int32_t)((int64_t)params[2] * s->env >> 15);              /* Q15 of 0.5 semitone */
    semis += (int32_t)((int64_t)depth * OP1_SINE_TABLE[s->vph >> 19] >> 15);  /* +-32768 Q16 = 0.5 semitone */
    s->semis = semis;
    int32_t m1 = exp2_q14(semis / 12);                         /* constant divisor: compiled as a multiply */
    if (m1 > 65535) m1 = 65535;
    /* volume: right side raised (X down) relative to home */
    int32_t dx = (s->sm[0] >> 8) - s->hx, g1 = 32767;
    if (dx < 0) { g1 = 32767 + (int32_t)((int64_t)params[3] * dx / VOL_FULL); if (g1 < 0) g1 = 0; }

    /* ---- per sample ---- */
    const int32_t *inc = OP1_PITCH_INC;
    const int16_t *sine = OP1_SINE_TABLE;
    int32_t tim = params[1];
    int32_t even = tim >> 2, drive = 8192 + (tim * 3 >> 2);    /* even harmonics up to 0.25, drive 1x .. 4x (Q13) */
    int32_t vg = op1_voice_vel_gain(self);
    int32_t m0 = s->mult, g0 = s->gain;
    uint32_t ph = s->ph;
    for (int n = 0; n < OP1_BLOCK; n++) {
        uint32_t mm = (uint32_t)(m0 + ((m1 - m0) * n >> 7));
        uint32_t i = (uint32_t)inc[n];
        ph += ((i >> 16) * mm << 2) + ((i & 0xFFFF) * mm >> 14);   /* i * mm / 16384, no 64-bit */
        int32_t x = sine[ph >> 19];
        x += (even * ((x * x) >> 15) >> 15) - (even >> 1);        /* x^2 = 2nd harmonic (minus its DC) */
        x = x * drive >> 13;                                       /* |x| < 41000, drive < 32768: 32-bit */
        if (x > 32767) x = 32767; else if (x < -32767) x = -32767;
        { int32_t x2 = (x * x) >> 15; x -= ((x2 * x) >> 15) * 10923 >> 15; }   /* soft clip */
        int32_t g = g0 + ((g1 - g0) * n >> 7);
        out[n] = op1_sat16((((x * g >> 15) * vg >> 15) * LEVEL) >> 15);   /* all 32-bit */
    }
    s->ph = ph; s->mult = m1; s->gain = g1;
}

const op1_engine_t ENGINE_aether = { ae_note_on, ae_render, 0, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_NONE } };

/* ======================= screen (GUI thread) =======================
 * aether.svg (gen_svg.py): cabinet, pitch rod, loop arm, knob bar tracks. Drawn here: the horizon (white, tilts
 * with the OP-1's side tilt, live sensor), the pitch dot on the rod (blue; the rod lit from the cabinet to it), the
 * loop (green, brightness = volume), a vibrato ripple beside the rod (white, while shaking) and the knob bars (page 2 on:
 * the bar shows the page-2 value and a dot sits under it). */
#include "aether_geom.h"
static const uint8_t C_WHITE[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, C_BLUE[4] = { 0x4D, 0x9E, 0xFF, 0xFF },
                     C_GREEN[4] = { 0x00, 0xED, 0x95, 0xFF }, C_RED[4] = { 0xFF, 0x3A, 0x5D, 0xFF },
                     C_GREY[4] = { 0x6F, 0x71, 0x73, 0xFF };
static int32_t sx, sz = 16384;        /* smoothed sensor (screen) */

static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)   /* Q10 */
{
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, w, c);
}
static void dim(uint8_t *o, const uint8_t *c, int32_t q15)        /* 25 % .. 100 % */
{
    int32_t l = 64 + (q15 * 192 >> 15);
    for (int i = 0; i < 3; i++) o[i] = (uint8_t)(c[i] * l >> 8);
    o[3] = 0xFF;
}
static int32_t isqrt(int32_t v) { int32_t r = 0, b = 1 << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

static void ae_screen_init(void *scr) { (void)scr; }

static void ae_screen_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    int slot = OP1_SCREEN_SLOT & 7;
    int16_t k[4];
    op1_synth_knobs_live(slot, k);
    const int16_t *w = op1_synth_knobs(slot);
    const ae_t *s = &st[ae_cur];
    /* horizon: rotated by the side tilt (X against Z), smoothed */
    sx += (AE_ACCEL[0] - sx) >> 2; sz += (AE_ACCEL[2] - sz) >> 2;
    int32_t n = isqrt(sx * sx + sz * sz); if (n < 1024) n = 1024;
    int32_t hx = (int32_t)((int64_t)OP1_Q10(AE_HOR_R) * sz / n), hy = (int32_t)((int64_t)OP1_Q10(AE_HOR_R) * sx / n);
    ln(r, OP1_Q10(AE_HOR_CX) - hx, OP1_Q10(AE_HOR_CY) + hy, OP1_Q10(AE_HOR_CX) + hx, OP1_Q10(AE_HOR_CY) - hy, 0x500, C_WHITE);
    /* pitch: dot on the rod, rod lit from the cabinet to it */
    int32_t y = OP1_Q10(AE_ROD_MID) - (int32_t)((int64_t)s->semis * AE_PITCH_PX_Q10 >> 16);
    if (y < OP1_Q10(AE_ROD_Y0)) y = OP1_Q10(AE_ROD_Y0);
    if (y > OP1_Q10(AE_ROD_Y1)) y = OP1_Q10(AE_ROD_Y1);
    ln(r, OP1_Q10(AE_ROD_X), OP1_Q10(AE_ROD_Y1), OP1_Q10(AE_ROD_X), y, 0x800, C_BLUE);
    ln(r, OP1_Q10(AE_ROD_X - 5), y - 0x1400, OP1_Q10(AE_ROD_X + 5), y + 0x1400, 0x700, C_BLUE);     /* dot = cross */
    ln(r, OP1_Q10(AE_ROD_X - 5), y + 0x1400, OP1_Q10(AE_ROD_X + 5), y - 0x1400, 0x700, C_BLUE);
    if (s->env > 2000) {                                           /* vibrato ripple: 3 small chevrons */
        int32_t a = OP1_Q10(3) * s->env >> 15;
        for (int i = 0; i < 3; i++) {
            int32_t yy = y + OP1_Q10(6 * i - 6), x = OP1_Q10(AE_ROD_X + 10);
            ln(r, x, yy, x + a, yy + OP1_Q10(2), 0x400, C_WHITE);
            ln(r, x + a, yy + OP1_Q10(2), x, yy + OP1_Q10(4), 0x400, C_WHITE);
        }
    }
    /* loop: 16-segment ellipse, green by volume */
    uint8_t c[4];
    dim(c, C_GREEN, s->gain);
    int32_t px = OP1_Q10(AE_LOOP_CX + AE_LOOP_RX), py = OP1_Q10(AE_LOOP_CY);
    for (int a = 512; a <= 8192; a += 512) {
        int32_t qx = OP1_Q10(AE_LOOP_CX) + (OP1_Q10(AE_LOOP_RX) * OP1_SINE_TABLE[(a + 2048) & 8191] >> 15);
        int32_t qy = OP1_Q10(AE_LOOP_CY) + (OP1_Q10(AE_LOOP_RY) * OP1_SINE_TABLE[a & 8191] >> 15);
        ln(r, px, py, qx, qy, 0x600, c); px = qx; py = qy;
    }
    /* knob bars (page-2 value while that page is on) */
    static const uint8_t *col[4] = { C_BLUE, C_GREEN, C_WHITE, C_RED };
    for (int i = 0; i < 4; i++) {
        int32_t v = i < 3 && op1_eng_page[i] ? (w[5 + i] > 0 ? w[5 + i] : 8192) : k[i];
        int32_t x0 = OP1_Q10(ae_bar_x[i] - AE_BAR_W / 2), len = (int32_t)((int64_t)OP1_Q10(AE_BAR_W) * v >> 15);
        if (len < 0x400) len = 0x400;
        ln(r, x0, OP1_Q10(AE_BAR_Y), x0 + len, OP1_Q10(AE_BAR_Y), 0x400, col[i]);
        if (i < 3 && op1_eng_page[i]) {                            /* page 2: a dot under the bar (two crossing segments) */
            int32_t cx = OP1_Q10(ae_bar_x[i]), cy = OP1_Q10(AE_BAR_Y + 7);           /* y 139.5 .. 142.5 */
            ln(r, cx - 0x600, cy, cx + 0x600, cy, 0xC00, col[i]);
            ln(r, cx, cy - 0x600, cx, cy + 0x600, 0xC00, col[i]);
        }
    }
    (void)C_GREY;
}

const op1_screen_t SCREEN_aether = { ae_screen_init, ae_screen_update };
