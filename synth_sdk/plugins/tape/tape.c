/* "tape" - simple tape delay (custom FX, own screen display/tapeecho.svg: vintage tape echo).
 * Knobs in OP-1 order, matching the screen elements left to right:
 *   knob 0 (blue)  : age       erase head: darker + more saturated repeats (low-pass in the loop)
 *   knob 1 (green) : feedback  record head: 0 .. ~110 % (soft-saturated, self-oscillates safely)
 *   knob 2 (white) : time      play head / tape speed: 30 ms .. 1.45 s, glides like a speed change
 *   knob 3 (red)   : wow & flutter depth, capstan: 0.6 Hz wow up to ~+-1.5 % pitch + 7 Hz flutter up to
 *                    ~+-0.4 % on the read head (squared knob curve: subtle low half, seasick at max)
 *
 * Reuses the built-in delay's scratch buffer for this FX position (synth/drum): 64 Ki int16
 * samples ring buffer (128 KiB of the 172 KiB area).
 */
#include "op1_plugin.h"

#define RING        65536u              /* samples, power of two */
#define MASK        (RING - 1)
#define DMIN        1323                /* 30 ms  */
#define DMAX        64000               /* 1.45 s */
#define WET         23000               /* wet level Q15 */

typedef struct {
    uint32_t w;         /* write index */
    uint32_t filled;    /* valid samples written since init (buffer starts with foreign data) */
    int32_t  d;         /* current delay, 22.10 fixed-point samples (smoothed) */
    uint32_t wow, flt;  /* LFO phases */
    int32_t  lp;        /* feedback low-pass state */
    int32_t  rng;
} tape_t;

static tape_t st[4];
static volatile int16_t in_level[3];   /* input peak (hold + decay) per FX position, for the screen LED */
static volatile int16_t fb_level[3];   /* feedback signal peak (hold + decay), record head brightness */
static volatile uint32_t samples[3];   /* samples processed per FX position: audio clock for the screen */

static inline int32_t sine(uint32_t ph) { return OP1_SINE_TABLE[((int32_t)ph >> 19) + 4096]; }

static inline int32_t softclip(int32_t x)          /* Q15 in/out, gentle cubic */
{
    if (x > 32767) x = 32767; else if (x < -32767) x = -32767;
    int32_t x3 = (((x * x) >> 15) * x) >> 15;
    return x - ((x3 * 10923) >> 15);                 /* x - x^3/3 */
}

static void tape_init(int pos)
{
    tape_t *t = &st[pos % 3];
    t->w = 0; t->filled = 0; t->lp = 0; t->wow = 0; t->flt = 0x40000000u;
    t->d = -1;                                      /* set from knob on first render */
}

static void tape_render(int pos, int16_t *buf, const int16_t *p)
{
    tape_t *t = &st[pos % 3];
    int16_t *ring = (int16_t *)op1_fx_scratch(pos);
    if (!ring) return;

    int32_t k0 = op1_fx_param_q15(p, 0), k1 = op1_fx_param_q15(p, 1);   /* raw words -> 0..32767 */
    int32_t k2 = op1_fx_param_q15(p, 2), k3 = op1_fx_param_q15(p, 3);
    int32_t target = DMIN + (int32_t)(((uint32_t)k2 * (DMAX - DMIN)) >> 15);     /* samples */
    int32_t fb     = (k1 * 36045) >> 15;                                          /* 0..1.1 Q15 */
    int32_t wowd   = (k3 * k3) >> 15;                                             /* depth, squared */
    int32_t age    = k0;
    int32_t lpk    = 32767 - ((age * 29000) >> 15);                               /* LP coeff */
    int32_t drive  = 32768 + (age << 1);                                          /* 1x..3x */
    if (t->d < 0) t->d = target << 10;

    int32_t peak = 0, inpk = 0, fbpk = 0;
    for (int n = 0; n < OP1_FX_BLOCK; n++) {
        /* tape-speed glide towards the target delay */
        t->d += ((target << 10) - t->d) >> 12;

        /* read-head modulation, 22.10 samples: wow 0.6 Hz up to 175 samples (2 pi 0.6 175 / 44100 = 1.5 %),
         * flutter 7 Hz up to 4 samples (0.4 %) */
        t->wow += 58436u;                    /* 0.6 Hz  */
        t->flt += 681788u;                   /* 7 Hz    */
        int32_t mod = (((sine(t->wow) * wowd) >> 15) * 175 >> 5) + (((sine(t->flt) * wowd) >> 15) * 4 >> 5);

        int32_t dd = t->d + mod;
        if (dd < (2 << 10)) dd = 2 << 10;
        uint32_t di = (uint32_t)dd >> 10, fr = (uint32_t)dd & 0x3ff;
        int32_t wet = 0;
        if (di + 1 < t->filled) {
            uint32_t r = (t->w - di) & MASK;
            int32_t a = ring[r], b = ring[(r - 1) & MASK];
            wet = a + (((b - a) * (int32_t)fr) >> 10);
        }

        /* age: low-pass + saturation in the loop */
        t->lp += ((wet - t->lp) * lpk) >> 15;
        int32_t loop = softclip((t->lp * drive) >> 15);

        int32_t in = buf[n];
        if (in > inpk) inpk = in; else if (-in > inpk) inpk = -in;
        int32_t fbs = (loop * fb) >> 15;
        if (fbs > fbpk) fbpk = fbs; else if (-fbs > fbpk) fbpk = -fbs;
        ring[t->w] = (int16_t)softclip(in + fbs);
        t->w = (t->w + 1) & MASK;
        if (t->filled < RING) t->filled++;

        int32_t o = in + ((t->lp * WET) >> 15);
        o = op1_sat16(o);
        buf[n] = (int16_t)o;
        if (o < 0) o = -o;
        if (o > peak) peak = o;
    }
    /* level meter like the built-in FX: peak hold with decay */
    volatile int16_t *m = op1_fx_meter(pos);
    int32_t mv = (*m * 0xfe00) >> 16;
    *m = (int16_t)(peak > mv ? peak : mv);
    int32_t il = (in_level[pos % 3] * 0xfc00) >> 16;
    in_level[pos % 3] = (int16_t)(inpk > il ? inpk : il);
    int32_t fl = (fb_level[pos % 3] * 0xfc00) >> 16;
    fb_level[pos % 3] = (int16_t)(fbpk > fl ? fbpk : fl);
    samples[pos % 3] += OP1_FX_BLOCK;
}

const op1_fx_t FX_tape = { tape_init, tape_render };

/* ======================= screen (GUI thread) =======================
 * display/tapeecho.svg (generated by display/gen_tape_svg.py, geometry in tape_geom.h): tape loop over
 * erase / record / play heads and the capstan, then through a serpentine of guide rollers. Each knob sits
 * above the part it controls, in OP-1 knob order (blue age -> erase head, green feedback -> record head,
 * white time -> play head, red wow -> capstan). Drawn here: the heads (erase blue / play white glow with
 * their knob; record red, glowing with the recorded signal = input + feedback), the input LED (filled disc,
 * knob row above the record head) -> summing node -> record head, play head -> feedback amp (green knob)
 * -> summing node, and moving gaps along the tape; spokes rotate.
 * Tape speed is locked to the audio: every frame the tape advances by the samples the DSP processed
 * since the last frame, at the DSP's current (smoothed) delay, so a mark needs exactly the delay time
 * from the record to the play head - independent of the screen frame rate. Stops while the FX is idle. */
#include "tape_geom.h"

#define GAP_STEP    44                             /* px between moving tape gaps */
static void *spokes[TP_ROLLERS], *pinch, *bars[4];
static uint32_t tpos;                             /* tape position, Q10 px */
static uint32_t rot;                              /* roller angle, 8192 per turn */
static uint32_t last_samples[3];
static const uint8_t COL_GAP[4]   = { 0x00, 0x00, 0x00, 0xFF };
static const uint8_t COL_ERASE[4] = { 0x4D, 0x9E, 0xFF, 0xFF };   /* blue  = age      */
static const uint8_t COL_REC[4]   = { 0xFF, 0x3A, 0x5D, 0xFF };   /* red: recorded signal */
static const uint8_t COL_PLAY[4]  = { 0xFF, 0xFF, 0xFF, 0xFF };   /* white = time     */
static const uint8_t COL_LED_OFF[4] = { 0x4E, 0x28, 0x32, 0xFF };  /* stock "offred"   */
static const uint8_t COL_LED_ON[4]  = { 0xFF, 0x42, 0x3F, 0xFF };  /* stock "orange"   */

static inline int32_t tsin(int32_t a) { return OP1_SINE_TABLE[a & 8191]; }   /* table[k] = sin(2 pi k/8192) */
static inline int32_t tcos(int32_t a) { return tsin(a + 2048); }

static void tape_screen_init(void *scr)
{
    for (int i = 0; i < TP_ROLLERS; i++) {                            /* ids s0 .. s99 */
        char id[4] = { 's', (char)('0' + (i < 10 ? i : i / 10)), (char)(i < 10 ? 0 : '0' + i % 10), 0 };
        spokes[i] = op1_find_node(scr, id);
    }
    pinch = op1_find_node(scr, "pinch");
    static const char *ids[4] = { "bar0", "bar1", "bar2", "bar3" };
    for (int i = 0; i < 4; i++) bars[i] = op1_find_node(scr, ids[i]);
}

/* point at distance d (px, 0 <= d < loop length) along the tape loop, Q10, + unit direction Q10 */
static void tape_point(int32_t d, op1_pt *p, int32_t *ux, int32_t *uy)
{
    int i = 0;
    while (i < TP_N - 1 && d >= tp_len[i]) d -= tp_len[i++];
    int j = (i + 1) % TP_N, L = tp_len[i];
    int32_t dx = tp_pt[j][0] - tp_pt[i][0], dy = tp_pt[j][1] - tp_pt[i][1];
    *ux = dx * 1024 / L; *uy = dy * 1024 / L;
    p->x = OP1_Q10(tp_pt[i][0]) + d * *ux; p->y = OP1_Q10(tp_pt[i][1]) + d * *uy;
}

static void line(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, w, c);
}

static void scale_col(uint8_t *o, const uint8_t *c, int32_t lvl)          /* lvl 0..256 */
{
    for (int i = 0; i < 3; i++) o[i] = (uint8_t)((c[i] * lvl) >> 8);
    o[3] = 0xFF;
}

static void head(void *r, int x, const uint8_t *c)                         /* U-shaped head + gap */
{
    int32_t x0 = OP1_Q10(x - 6), x1 = OP1_Q10(x + 6), y0 = OP1_Q10(TP_HEAD_TOP), y1 = OP1_Q10(TP_TOP_Y - 2);
    line(r, x0, y1, x0, y0, 0x600, c);
    line(r, x0, y0, x1, y0, 0x600, c);
    line(r, x1, y0, x1, y1, 0x600, c);
    line(r, OP1_Q10(x), OP1_Q10(TP_HEAD_TOP + 3), OP1_Q10(x), y1, 0x400, c);
}

/* filled disc from 1-px horizontal scanlines (raster_line has no circle primitive); r in Q10 px */
#define LED_R  (9 * 512)                          /* 4.5 px */
static void disc(void *r, int cx, int cy, int32_t rad, const uint8_t *c)
{
    for (int32_t dy = -rad + 512; dy < rad; dy += 1024) {          /* row centres */
        int32_t h2 = (rad * rad - dy * dy) >> 10, w = 0;           /* half width^2, Q10 */
        while ((w + 32) * (w + 32) >> 10 <= h2) w += 32;            /* isqrt, 1/32 px steps */
        if (!w) continue;
        line(r, OP1_Q10(cx) - w, OP1_Q10(cy) + dy, OP1_Q10(cx) + w, OP1_Q10(cy) + dy, 0x600, c);
    }
}

static const uint8_t COL_FB[4]    = { 0x00, 0xED, 0x95, 0xFF };   /* feedback path (green) */
static const uint8_t COL_LANE[4]  = { 0xDF, 0xD9, 0xFF, 0xFF };   /* summing node -> record head */

/* signal lane: input LED -> sum -> record head; play head -> feedback amp -> sum */
static void lanes(void *r, int32_t inlvl, int32_t fb)            /* inlvl 0..256, fb 0..32767 */
{
    uint8_t c[4];
    int32_t ly = OP1_Q10(TP_LANE_Y), sx = OP1_Q10(TP_SUM_X), ax = OP1_Q10(TP_AMP_X);
    int32_t px = OP1_Q10(TP_PLAY_X - TP_HEAD_HW), hy = OP1_Q10(TP_HEAD_TOP);
    /* input: LED -> summing node, orange with the input level */
    for (int i = 0; i < 3; i++) c[i] = (uint8_t)(COL_LED_OFF[i] + (((COL_LED_ON[i] - COL_LED_OFF[i]) * inlvl) >> 8));
    c[3] = 0xFF;
    line(r, sx, OP1_Q10(TP_LED_Y) + 0x1A00, sx, ly - 0xE00, 0x600, c);     /* LED down to the node */
    /* feedback: play head (left corner) up to the lane, left to the amp: dim green */
    scale_col(c, COL_FB, 90);
    line(r, px, hy - 0x400, px, ly, 0x600, c);
    line(r, px, ly, ax + 0x800, ly, 0x600, c);
    /* amp -> summing node: green 15 % .. 100 % with the feedback amount; amp triangle pointing left */
    scale_col(c, COL_FB, 38 + (fb * 218 >> 15));
    line(r, ax - 0x1400, ly, sx + 0xE00, ly, 0x600, c);
    int32_t tl = ax - 0x1400, tr = ax + 0x800, th = 0x1400;              /* 7 px wide, 10 px high */
    line(r, tr, ly - th / 2 - 0x200, tr, ly + th / 2 + 0x200, 0x600, c);
    line(r, tr, ly - th / 2 - 0x200, tl, ly, 0x600, c);
    line(r, tr, ly + th / 2 + 0x200, tl, ly, 0x600, c);
    /* summing node -> record head */
    line(r, sx, ly + 0xE00, sx, hy - 0x400, 0x600, COL_LANE);
}

static void tape_screen_update(void *scr, int dt)
{
    int pos = op1_fx_screen_pos();
    int16_t k[4];
    op1_fx_knobs_live(k);

    /* tape advance = (rec -> play distance) * elapsed samples / current delay in samples */
    uint32_t now = samples[pos % 3], ds = now - last_samples[pos % 3];
    last_samples[pos % 3] = now;
    if (ds > 8192) ds = 8192;                                          /* after a pause: no jump */
    int32_t delay = st[pos % 3].d >> 10;                               /* DSP's smoothed delay, samples */
    if (delay < DMIN) delay = DMIN + (int32_t)(((uint32_t)k[2] * (DMAX - DMIN)) >> 15);   /* not run yet */
    int32_t v = (int32_t)((uint32_t)(TP_PLAY_X - TP_REC_X) * 1024u * ds / (uint32_t)delay);   /* Q10 px */
    tpos += (uint32_t)v;
    rot += (uint32_t)v * 261u >> 10;                                    /* px / (2 pi R) * 8192 */

    for (int i = 0; i < 4; i++) if (bars[i]) {                        /* dials: -135..+135 degrees */
        int32_t a = ((int32_t)k[i] * 6144 >> 15) - 3072;
        op1_node_rotate(bars[i], OP1_Q10(tp_dial[i][0]), OP1_Q10(tp_dial[i][1]), tsin(a), tcos(a));
    }
    for (int i = 0; i < TP_ROLLERS; i++) if (spokes[i]) {
        int32_t a = (int32_t)rot * tp_roll[i][2];
        op1_node_rotate(spokes[i], tp_roll[i][0], tp_roll[i][1], tsin(a), tcos(a));
    }
    if (pinch) { int32_t a = -(int32_t)rot; op1_node_rotate(pinch, OP1_Q10(TP_CAPSTAN_X), OP1_Q10(TP_TOP_Y - 6), tsin(a), tcos(a)); }
    op1_screen_draw(scr, dt);

    void *r = op1_raster();
    int32_t off = (int32_t)((tpos >> 10) % GAP_STEP);                   /* moving gaps along the loop */
    for (int32_t d = off; d < TP_LOOP_LEN; d += GAP_STEP) {
        op1_pt a, b; int32_t ux, uy;
        tape_point(d, &a, &ux, &uy);
        b.x = a.x + ux * 2; b.y = a.y + uy * 2;
        op1_raster_line(r, &a, &b, 0x800, COL_GAP);
    }

    /* heads: erase / play glow 28..100 % with their knob, record with input + feedback signal */
    uint8_t c[4];
    scale_col(c, COL_ERASE, 72 + ((int32_t)k[0] * 184 >> 15));
    head(r, TP_ERASE_X, c);
    scale_col(c, COL_PLAY, 72 + ((int32_t)k[2] * 184 >> 15));
    head(r, TP_PLAY_X, c);
    int32_t rec = (in_level[pos % 3] + fb_level[pos % 3]) >> 6; if (rec > 256) rec = 256;
    scale_col(c, COL_REC, 72 + (rec * 184 >> 8));
    head(r, TP_REC_X, c);

    /* input LED: off-red -> orange with the input peak, plus a hot white core when loud */
    int32_t lvl = in_level[pos % 3] >> 6; if (lvl > 256) lvl = 256; if (lvl < 0) lvl = 0;
    lanes(r, lvl, k[1]);
    for (int i = 0; i < 3; i++) c[i] = (uint8_t)(COL_LED_OFF[i] + (((COL_LED_ON[i] - COL_LED_OFF[i]) * lvl) >> 8));
    c[3] = 0xFF;
    disc(r, TP_LED_X, TP_LED_Y, LED_R, c);
    if (lvl > 192) {
        scale_col(c, (const uint8_t[4]){ 0xFF, 0xD0, 0xC0, 0xFF }, (lvl - 192) * 4);
        disc(r, TP_LED_X, TP_LED_Y, LED_R / 2, c);
    }
}

const op1_screen_t SCREEN_tape = { tape_screen_init, tape_screen_update };
