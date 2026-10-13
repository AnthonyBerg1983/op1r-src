/* Granular engine over the slot's sampler recording.
 *
 * Workflow on the OP-1: pick "sampler" on a synth slot, record (mic/line/radio as usual),
 * then switch the same slot to "granular". The recording is read live from the sampler buffer
 * (and reloaded from synth_<slot>.raw at boot thanks to the uses_sample hook).
 *
 *   knob 0 (blue)  : position   - where in the recording grains start (0 .. recorded length)
 *   knob 1 (green) : grain size - ~10 ms .. ~370 ms
 *   knob 2 (white) : spray      - random start jitter around position (0 .. ~1.5 s)
 *   knob 3 (orange): density    - grains per grain-length, 0.5x .. 4x overlap
 *   red press      : page 2 = sample gain (params[7]: 1..32767 = 0..4x linear, 8192 = 1x; 0 = 1x, the
 *                    default of existing patches)
 *   keyboard       : pitch (C4 plays the recording at original speed); random +-detune per grain
 *                    when spray is up
 *
 * Up to 4 overlapping grains per voice, Hann-ish window (sin^2 from the firmware sine table),
 * linear interpolation, 22.10 fixed-point sample positions.
 */
#include "op1_plugin.h"

#define MAX_GRAINS   4
#define FRAC         10                        /* position fraction bits */
#define INC_C4       25480370u                 /* phase inc of 261.63 Hz @44.1 kHz (2^32 = 1 cyc/sample) */

typedef struct {
    uint32_t pos;       /* 22.10 sample position */
    uint32_t rate;      /* 22.10 per sample */
    uint32_t wph;       /* window phase, 0..2^31 = half sine cycle */
    uint32_t winc;      /* window phase increment (0 = inactive) */
} grain_t;

typedef struct {
    grain_t  g[MAX_GRAINS];
    int32_t  next;      /* samples until next grain spawn */
    int32_t  len;       /* usable recording length (samples) */
    uint32_t rng;
} gvoice_t;

static gvoice_t gv[OP1_VOICES];

static inline uint32_t rnd(gvoice_t *v) { v->rng = v->rng * 1664525u + 1013904223u; return v->rng; }

/* recorded length = end of the last peak bucket above a small threshold */
static int32_t recorded_length(const uint8_t *sb)
{
    const int16_t *pk = op1_sample_peaks(sb);
    for (int i = OP1_PEAK_BUCKETS - 1; i >= 0; i--)
        if (pk[i] > 96) {
            int32_t l = (i + 1) * 256;
            return l > OP1_SAMPLE_MAX ? OP1_SAMPLE_MAX : l;
        }
    return 0;
}

static void gr_note_on(void *self, const int16_t *params)
{
    gvoice_t *v = &gv[op1_voice_index(self)];
    const uint8_t *sb = op1_sample_buffer(op1_current_slot());
    (void)params;
    for (int i = 0; i < MAX_GRAINS; i++) v->g[i].winc = 0;
    v->next = 0;
    v->len = sb ? recorded_length(sb) : 0;
    v->rng ^= (uint32_t)self * 2654435761u + 12345u;
}

static void spawn(gvoice_t *v, const int16_t *params, uint32_t rate, int32_t size)
{
    int i;
    for (i = 0; i < MAX_GRAINS; i++) if (!v->g[i].winc) break;
    if (i == MAX_GRAINS) return;                      /* all busy: skip this grain */
    grain_t *g = &v->g[i];

    int32_t len   = v->len;
    int32_t start = (int32_t)(((uint64_t)(uint16_t)params[0] * (uint32_t)len) >> 15);   /* 64-bit: no wrap */
    int32_t spray = (int32_t)(((uint32_t)params[2] * 66150u) >> 15);   /* up to 1.5 s */
    if (spray) start += (int32_t)(rnd(v) % (uint32_t)(2 * spray + 1)) - spray;
    if (start < 0) start = 0;
    if (start >= len) start = len - 1;

    /* a little random detune (+-~1%) proportional to spray */
    uint32_t r = rate;
    if (params[2] > 4096) {
        int32_t dt = (int32_t)(rnd(v) >> 22) - 512;                     /* -512..511 */
        r = (uint32_t)((int32_t)rate + ((int32_t)(rate >> 10) * dt * (params[2] >> 8) >> 12));
    }
    g->pos  = (uint32_t)start << FRAC;
    g->rate = r;
    g->wph  = 0;
    g->winc = 0x80000000u / (uint32_t)size;           /* window spans `size` output samples */
}

static void gr_render(void *self, int16_t *out, const int16_t *params)
{
    gvoice_t *v = &gv[op1_voice_index(self)];
    const uint8_t *sb = op1_sample_buffer(op1_current_slot());
    const int16_t *sine = OP1_SINE_TABLE;

    if (!sb || v->len < 256) {                        /* nothing recorded yet */
        if (sb) v->len = recorded_length(sb);
        for (int n = 0; n < OP1_BLOCK; n++) out[n] = 0;
        return;
    }
    const int16_t *data = op1_sample_data(sb);
    int32_t len = v->len;

    /* playback rate from the keyboard pitch (constant over the block) */
    uint32_t rate = (uint32_t)OP1_PITCH_INC[0] / (INC_C4 >> FRAC);
    if (rate < 16) rate = 16;

    int32_t size = 441 + (int32_t)(((uint32_t)params[1] * 16000u) >> 15);   /* 10 ms .. 373 ms */
    /* density: spawn interval = size / overlap, overlap 0.5 .. 4 */
    int32_t ovl8 = 4 + (params[3] >> 10);                                  /* overlap * 8: 4..35 */
    int32_t interval = (size * 8) / ovl8;
    if (interval < 32) interval = 32;
    /* gain compensation for overlapping grains */
    int32_t gain = ovl8 <= 8 ? 32767 : (32767 * 8) / ovl8 + 6000;
    if (gain > 32767) gain = 32767;
    gain = (gain * op1_voice_vel_gain(self)) >> 15;                         /* note velocity */
    int32_t sg = params[7] > 0 ? params[7] : 8192;                         /* sample gain, Q13 */

    for (int n = 0; n < OP1_BLOCK; n++) {
        if (--v->next <= 0) {
            spawn(v, params, rate, size);
            v->next = interval;
        }
        int32_t acc = 0;
        for (int i = 0; i < MAX_GRAINS; i++) {
            grain_t *g = &v->g[i];
            if (!g->winc) continue;
            uint32_t idx = g->pos >> FRAC;
            if ((int32_t)idx >= len - 1) { g->winc = 0; continue; }
            int32_t a = data[idx], b = data[idx + 1];
            int32_t s = a + (((b - a) * (int32_t)(g->pos & ((1 << FRAC) - 1))) >> FRAC);
            int32_t w = sine[((int32_t)g->wph >> 19) + 4096];             /* sin(pi*t), 0..32767 */
            w = (w * w) >> 15;                                            /* sin^2 = Hann */
            acc += (s * w) >> 15;
            g->pos += g->rate;
            g->wph += g->winc;
            if (g->wph >= 0x80000000u) g->winc = 0;                       /* grain finished */
        }
        out[n] = op1_sat16((((acc * gain) >> 15) * sg) >> 13);          /* |x| < 2^16, sg < 2^15: 32 bit */
    }
}

const op1_engine_t ENGINE_granular = { gr_note_on, gr_render, 0, { 0, 0, 0, OP1_PRESS_PAGE } };

/* ======================= screen (GUI thread) =======================
 * display/granular.svg: frame for the sample window (x 20..300, y 20..100) + four dials (bar0..3).
 * Drawn here each frame: recorded waveform (peak table), position marker + spray range, live grains. */
#define WX0 20
#define WX1 300
#define WYC 60
#define WH  38            /* half height in px */

static void *bars[4];
static const uint8_t COL_WAVE[4] = { 0x6F, 0x71, 0x73, 0xFF };
static const uint8_t COL_POS[4]  = { 0x4D, 0x9E, 0xFF, 0xFF };   /* blue  = position */
static const uint8_t COL_SIZE[4] = { 0x00, 0xED, 0x95, 0xFF };   /* green = grain size bar */
static const uint8_t COL_SPRAY[4]= { 0xFF, 0xFF, 0xFF, 0xFF };   /* white = spray range */
static const uint8_t COL_GRAIN[4]= { 0xFF, 0x3A, 0x5D, 0xFF };   /* orange/red = grains */
static const int DIAL_X[4] = { 70, 130, 190, 250 };

static void gr_screen_init(void *scr)
{
    static const char *ids[4] = { "bar0", "bar1", "bar2", "bar3" };
    for (int i = 0; i < 4; i++) bars[i] = op1_find_node(scr, ids[i]);
}

static inline void vline(void *r, int32_t x, int32_t y0, int32_t y1, const uint8_t *c)
{
    op1_pt a = { x, y0 }, b = { x, y1 };
    op1_raster_line(r, &a, &b, 0x300, c);
}
static inline void hline(void *r, int32_t x0, int32_t x1, int32_t y, const uint8_t *c)
{
    op1_pt a = { x0, y }, b = { x1, y };
    op1_raster_line(r, &a, &b, 0x300, c);
}
static inline int32_t xpos(int32_t sample, int32_t len)            /* sample -> Q10 x */
{
    if (len <= 0) return OP1_Q10(WX0);
    if (sample < 0) sample = 0;
    if (sample > len) sample = len;
    while (len > 0xFFFFF) { len >>= 1; sample >>= 1; }             /* keep sample << 11 within 31 bits */
    return OP1_Q10(WX0) + ((sample << 11) / len) * ((WX1 - WX0) * 1024 / 2048);   /* 32-bit only */
}

static void gr_screen_update(void *scr, int dt)
{
    int slot = OP1_SCREEN_SLOT & 7;
    int16_t k[4];
    op1_synth_knobs_live(slot, k);                 /* includes LFO modulation, like the stock screens */

    /* dials: knob 0..32767 -> -135..+135 degrees (firmware sine table: 8192 = one turn) */
    int pg = op1_eng_page[3];                                          /* red page 2: needle = sample gain */
    if (pg) k[3] = op1_synth_knobs(slot)[7] > 0 ? op1_synth_knobs(slot)[7] : 8192;
    for (int i = 0; i < 4; i++) if (bars[i]) {
        int32_t a = ((int32_t)k[i] * 6144 >> 15) - 3072;              /* -3072..3072 = -135..135 deg */
        int32_t s = OP1_SINE_TABLE[a & 8191];                         /* table[k] = sin(2*pi*k/8192) */
        int32_t c = OP1_SINE_TABLE[(a + 2048) & 8191];
        op1_node_rotate(bars[i], OP1_Q10(DIAL_X[i]), OP1_Q10(133), s, c);
    }
    op1_screen_draw(scr, dt);                                          /* SVG first, then overlays */

    void *r = op1_raster();
    if (pg) {                                                          /* page-2 ring around the red dial */
        op1_pt p = { OP1_Q10(DIAL_X[3]) + OP1_Q10(17), OP1_Q10(133) };
        for (int a = 512; a <= 8192; a += 512) {
            op1_pt q = { OP1_Q10(DIAL_X[3]) + (OP1_Q10(17) * OP1_SINE_TABLE[(a + 2048) & 8191] >> 15),
                         OP1_Q10(133) + (OP1_Q10(17) * OP1_SINE_TABLE[a & 8191] >> 15) };
            op1_raster_line(r, &p, &q, 0x500, COL_GRAIN);
            p = q;
        }
    }
    const uint8_t *sb = op1_sample_buffer(slot);
    if (!sb) return;
    const int16_t *pk = op1_sample_peaks(sb);
    int32_t len = recorded_length(sb);
    if (len < 256) {                                                   /* nothing recorded: flat line */
        hline(r, OP1_Q10(WX0), OP1_Q10(WX1), OP1_Q10(WYC), COL_WAVE);
        return;
    }
    /* waveform: one column per 2 px from the 256-sample peak table */
    int nb = (len + 255) >> 8;
    for (int x = 0; x < WX1 - WX0; x += 2) {
        int b0 = x * nb / (WX1 - WX0), b1 = (x + 2) * nb / (WX1 - WX0);
        int m = 0;
        for (int b = b0; b <= b1 && b < nb; b++) if (pk[b] > m) m = pk[b];
        int32_t h = (m * WH) >> 15; if (h < 1) h = 1;
        vline(r, OP1_Q10(WX0 + x), OP1_Q10(WYC - h), OP1_Q10(WYC + h), COL_WAVE);
    }
    /* position marker, spray range (white, under the window), grain size (green, from position) */
    int32_t pos = (int32_t)(((uint64_t)(uint16_t)k[0] * (uint32_t)len) >> 15);
    int32_t spray = (int32_t)(((uint32_t)k[2] * 66150u) >> 15);
    int32_t size = 441 + (int32_t)(((uint32_t)k[1] * 16000u) >> 15);
    int32_t s0 = pos - spray < 0 ? 0 : pos - spray, s1 = pos + spray > len ? len : pos + spray;
    hline(r, xpos(s0, len), xpos(s1, len), OP1_Q10(WYC + WH + 6), COL_SPRAY);
    hline(r, xpos(pos, len), xpos(pos + size > len ? len : pos + size, len), OP1_Q10(WYC - WH - 6), COL_SIZE);
    vline(r, xpos(pos, len), OP1_Q10(WYC - WH - 2), OP1_Q10(WYC + WH + 2), COL_POS);

    /* live grains of all voices: height = current window amplitude */
    for (int v = 0; v < OP1_VOICES; v++)
        for (int i = 0; i < MAX_GRAINS; i++) {
            const grain_t *g = &gv[v].g[i];
            if (!g->winc) continue;
            int32_t w = OP1_SINE_TABLE[(g->wph >> 19) & 8191];           /* sin(pi*t), 0..32767 */
            int32_t h = 2 + ((w * (WH - 4)) >> 15);
            int32_t x = xpos((int32_t)(g->pos >> FRAC), len);
            vline(r, x, OP1_Q10(WYC - h), OP1_Q10(WYC + h), COL_GRAIN);
        }
}

const op1_screen_t SCREEN_granular = { gr_screen_init, gr_screen_update };
