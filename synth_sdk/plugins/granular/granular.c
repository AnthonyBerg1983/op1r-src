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
 *   white press    : sample browser (core, screens/sbrowse.c): the sampler presets, as sampler+
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
    volatile uint32_t blocks;   /* render calls: the screen shows a voice's grains only while it renders */
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
    v->blocks++;

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

const op1_engine_t ENGINE_granular = { gr_note_on, gr_render, 0, { 0, 0, OP1_PRESS_BROWSE, OP1_PRESS_PAGE } };

/* ======================= screen (GUI thread) =======================
 * granular.svg: the sample window frame (x 20..300, y 20..90). Drawn here each frame: the recorded waveform (peak
 * table), position marker + spray range + grain size, live grains; below, as sampler+ T1: a bar per knob (y 110),
 * its label (y 114) and value (y 127) in the stock font, "file" above the white bar (y 94: white press = the sample
 * browser, as sampler+ T1). Red page 2: label / value "gain". */
#include "op1_synth.h"                    /* stock-font text (firmware calls, no imports) */
#define WX0 20
#define WX1 300
#define WYC 55
#define WH  29            /* half height in px */
#define TS  12
#define Q(v) ((v) << 10)

static const uint8_t COL_WAVE[4] = { 0x6F, 0x71, 0x73, 0xFF };
static const uint8_t COL_POS[4]  = { 0x4D, 0x9E, 0xFF, 0xFF };   /* blue  = position */
static const uint8_t COL_SIZE[4] = { 0x00, 0xED, 0x95, 0xFF };   /* green = grain size */
static const uint8_t COL_SPRAY[4]= { 0xFF, 0xFF, 0xFF, 0xFF };   /* white = spray range */
static const uint8_t COL_GRAIN[4]= { 0xFF, 0x3A, 0x5D, 0xFF };   /* red   = grains, density */
static const uint8_t *const COL4[4] = { COL_POS, COL_SIZE, COL_SPRAY, COL_GRAIN };
static uint32_t gt_lab[4][OP1_TEXT_SIZE / 4], gt_val[4][OP1_TEXT_SIZE / 4], gt_file[OP1_TEXT_SIZE / 4];
static char gt_shown[9][12];
static int gt_made;

static void gr_screen_init(void *scr) { (void)scr; }

static inline void vline(void *r, int32_t x, int32_t y0, int32_t y1, const uint8_t *c)
{
    op1_pt a = { x, y0 }, b = { x, y1 };
    op1_raster_line(r, &a, &b, 0x300, c);
}
static inline void hline(void *r, int32_t x0, int32_t x1, int32_t y, const uint8_t *c, int32_t w)
{
    op1_pt a = { x0, y }, b = { x1, y };
    if (x1 > x0) op1_raster_line(r, &a, &b, w, c);
}
static inline int32_t xpos(int32_t sample, int32_t len)            /* sample -> Q10 x */
{
    if (len <= 0) return Q(WX0);
    if (sample < 0) sample = 0;
    if (sample > len) sample = len;
    while (len > 0xFFFFF) { len >>= 1; sample >>= 1; }             /* keep sample << 11 within 31 bits */
    return Q(WX0) + ((sample << 11) / len) * ((WX1 - WX0) * 1024 / 2048);   /* 32-bit only */
}
static char *gt_num(char *p, int32_t v, int dec)                   /* v in 1/10^dec, e.g. 125, 2 -> "1.25" */
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v || n <= dec);
    while (n) { *p++ = t[--n]; if (n == dec && dec) *p++ = '.'; }
    return p;
}
static char *gt_cat(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static void gt_ms(char *s, int32_t samples)                         /* "120ms", "1.25s" */
{
    int32_t ms = (samples * 10 + 220) / 441;
    char *p = ms < 1000 ? gt_cat(gt_num(s, ms, 0), "ms") : gt_cat(gt_num(s, ms / 10, 2), "s");
    *p = 0;
}
static void gt_text(void *r, uint32_t *t, char *shown, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1, j = 0;
    for (; s[j] || shown[j]; j++) if (s[j] != shown[j]) { same = 0; break; }
    if (!same) { op1_text_set(t, s); for (j = 0; j < 11 && s[j]; j++) shown[j] = s[j]; shown[j] = 0; }
    op1_text_draw(t, r, ident);
}

static void gr_screen_update(void *scr, int dt)
{
    int slot = OP1_SCREEN_SLOT & 7;
    int16_t k[4];
    op1_synth_knobs_live(slot, k);                 /* includes LFO modulation, like the stock screens */
    int pg = op1_eng_page[3];                      /* red page 2: sample gain */
    if (!gt_made) {                                /* stock-font texts: built once, after boot (GUI thread) */
        for (int i = 0; i < 4; i++) {
            op1_text_ctor(gt_lab[i], Q(8 + 78 * i), 114, TS, COL4[i]);
            op1_text_ctor(gt_val[i], Q(8 + 78 * i), 127, TS, COL_SPRAY);
        }
        op1_text_ctor(gt_file, Q(8 + 78 * 2), 94, TS, COL_SPRAY);    /* white press: the browser (as sampler+) */
        for (int i = 0; i < 9; i++) gt_shown[i][0] = 0;
        gt_made = 1;
    }
    op1_screen_draw(scr, dt);                                          /* SVG first, then overlays */
    void *r = op1_raster();

    const uint8_t *sb = op1_sample_buffer(slot);
    int32_t len = sb ? recorded_length(sb) : 0;
    int32_t pos = (int32_t)(((uint64_t)(uint16_t)k[0] * (uint32_t)len) >> 15);
    int32_t size = 441 + (int32_t)(((uint32_t)k[1] * 16000u) >> 15);
    int32_t spray = (int32_t)(((uint32_t)k[2] * 66150u) >> 15);
    int32_t ovl8 = 4 + (k[3] >> 10);                                   /* as gr_render */

    /* bottom row: bars, labels, values */
    for (int i = 0; i < 4; i++) hline(r, Q(8 + 78 * i), Q(78 + 78 * i), Q(110), COL4[i], 0x800);
    static const char *const lab[5] = { "pos", "size", "spray", "dens", "gain" };
    char v[4][12];
    *gt_cat(gt_num(v[0], (pos + 220) / 441, 2), "s") = 0;              /* position always in s: 0.00s .. */
    gt_ms(v[1], size);
    gt_ms(v[2], spray);
    if (!pg) *gt_cat(gt_num(v[3], ovl8 * 10 / 8, 1), "x") = 0;          /* overlap 0.5 .. 4.3x */
    else {
        int32_t g = op1_synth_knobs(slot)[7] > 0 ? op1_synth_knobs(slot)[7] : 8192;
        *gt_cat(gt_num(v[3], g * 10 / 8192, 1), "x") = 0;               /* sample gain 0 .. 4x */
    }
    for (int i = 0; i < 4; i++) {
        gt_text(r, gt_lab[i], gt_shown[i], lab[i == 3 && pg ? 4 : i]);
        gt_text(r, gt_val[i], gt_shown[4 + i], v[i]);
    }
    gt_text(r, gt_file, gt_shown[8], "file");

    if (len < 256) {                                                   /* nothing recorded: flat line */
        hline(r, Q(WX0), Q(WX1), Q(WYC), COL_WAVE, 0x300);
        return;
    }
    const int16_t *pk = op1_sample_peaks(sb);
    int nb = (len + 255) >> 8;                                         /* waveform: a column per 2 px */
    for (int x = 0; x < WX1 - WX0; x += 2) {
        int b0 = x * nb / (WX1 - WX0), b1 = (x + 2) * nb / (WX1 - WX0);
        int m = 0;
        for (int b = b0; b <= b1 && b < nb; b++) if (pk[b] > m) m = pk[b];
        int32_t h = (m * WH) >> 15; if (h < 1) h = 1;
        vline(r, Q(WX0 + x), Q(WYC - h), Q(WYC + h), COL_WAVE);
    }
    /* spray range (white, under the window), grain size (green, above), position marker */
    int32_t s0 = pos - spray < 0 ? 0 : pos - spray, s1 = pos + spray > len ? len : pos + spray;
    hline(r, xpos(s0, len), xpos(s1, len), Q(WYC + WH + 3), COL_SPRAY, 0x300);
    hline(r, xpos(pos, len), xpos(pos + size > len ? len : pos + size, len), Q(WYC - WH - 3), COL_SIZE, 0x300);
    vline(r, xpos(pos, len), Q(WYC - WH - 1), Q(WYC + WH + 1), COL_POS);

    /* live grains of the voices rendering since the last frame (a finished voice is not rendered any more and keeps
     * its last grains): height = current window amplitude */
    static uint32_t seen[OP1_VOICES];
    for (int vi = 0; vi < OP1_VOICES; vi++) {
        uint32_t b = gv[vi].blocks;
        int live = b != seen[vi];
        seen[vi] = b;
        if (!live) continue;
        for (int i = 0; i < MAX_GRAINS; i++) {
            const grain_t *g = &gv[vi].g[i];
            if (!g->winc) continue;
            int32_t w = OP1_SINE_TABLE[(g->wph >> 19) & 8191];           /* sin(pi*t), 0..32767 */
            int32_t h = 2 + ((w * (WH - 4)) >> 15);
            vline(r, xpos((int32_t)(g->pos >> FRAC), len), Q(WYC - h), Q(WYC + h), COL_GRAIN);
        }
    }
}

const op1_screen_t SCREEN_granular = { gr_screen_init, gr_screen_update };
