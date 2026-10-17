/* MIDI sequencer view drawing (docs/39): one function, compiled in the firmware (ms_fw.c, every frame) and on the
 * host (harness/mseq_render.c -> render_mseq.py preview). Lines only (sq_line / the stroke font of sq_text). */
#ifndef OP1_HOST
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#endif
#include "../op1_synth.h"
#include "../seq/seq_text.h"
#include "ms_draw.h"

/* stock-font text (firmware Text objects, docs/18 / docs/23): made once, laid out only when the string changes */
#define TXT_SIZE 12
#ifdef OP1_HOST
void host_text_align(void *t, int align);
#define TXT_CTOR(o, x, y, c, a) (host_text_ctor((o), (x), (y), TXT_SIZE, (c)), host_text_align((o), (a)))
#else
#define TXT_CTOR(o, x, y, c, a) (((void *(*)(void *, int32_t, int32_t, int32_t, const uint8_t *, int32_t))0x01A61904)( \
                                    (o), (x), (y), TXT_SIZE, (c), (a)))           /* align: 0 left, 1 centred, 2 right */
#endif
typedef struct { uint32_t mem[OP1_TEXT_SIZE / 4]; char last[12]; uint8_t made; } ms_txt;
static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
/* drawn width of the text (q10) from the bounds set_text computed (docs/45: +0x90 x_min, +0x94 x_max); drawing
 * scales them once more by the display factor set_text uses (0x500 / 0x4B3 / [0x01DFB654]; emulator: 1.25) */
#ifdef OP1_HOST
int32_t host_text_width(const void *t);
static int32_t txt_w(const ms_txt *t) { return host_text_width(t->mem); }
#else
static int32_t txt_w(const ms_txt *t)
{
    int32_t f = *(volatile int32_t *)0x01DFB654;
    if (*(volatile uint8_t *)0xFF906A54 == 1 && !*(volatile uint8_t *)0xFF906A56)
        f = !*(volatile uint8_t *)0xFF906A55 && !*(volatile uint8_t *)0x018BE698 ? 0x500 : 0x4B3;
    const int32_t *m = (const int32_t *)t->mem;
    return (int32_t)(((int64_t)(m[0x94 / 4] - m[0x90 / 4]) * f) >> 10);
}
#endif
static void txt(void *r, ms_txt *t, int32_t x, int32_t y, const uint8_t *c, int align, const char *str)
{
    if (!t->made) { TXT_CTOR(t->mem, x, y, c, align); t->made = 1; t->last[0] = 1; t->last[1] = 0; }
    int k = 0;
    while (k < 11 && str[k] && str[k] == t->last[k]) k++;
    if (str[k] != t->last[k]) { op1_text_set(t->mem, str); for (k = 0; k < 11 && (t->last[k] = str[k]) != 0; k++) ; t->last[11] = 0; }
    op1_text_draw(t->mem, r, ident);
}
enum { T_TRACK_W, T_TRACK_G, T_POS, T_MIN, T_COLON, T_S1, T_S2, T_DOT, T_H1, T_H2, T_IN_W, T_IN_G, T_OUT_W, T_OUT_G, T_Q, T_N };
static ms_txt tx[T_N];

static const uint8_t c_lane[4] = { 0x6F, 0x71, 0x73, 255 }, c_bar[4] = { 0x48, 0x4A, 0x52, 255 },
                     c_white[4] = { 255, 255, 255, 255 }, c_blue[4] = { 0x4D, 0x9E, 0xFF, 255 },
                     c_green[4] = { 0x00, 0xED, 0x95, 255 }, c_red[4] = { 0xFF, 0x3A, 0x5D, 255 },
                     c_grey[4] = { 0x6F, 0x71, 0x73, 255 }, c_dim[4] = { 0x35, 0x32, 0x38, 255 };

#define X0      OP1_Q10(4)                   /* lanes: x from here to X1 */
#define X1      OP1_Q10(318)
#define LANE_Y(t) OP1_Q10(34 + (t) * 26)     /* top of lane t, 26 px each, separators below */
#define BAND_Y  OP1_Q10(28)                  /* loop bar / marker carets */
#define TXT_Y   4                            /* status bar text, px */
#define QUANT_X 80                           /* quantize grid: at least here, else after the position text */
#define TIME_X  160                          /* time: second digit's cell centre, px */
#define ARROW_X 280                          /* input -> output arrow, px */

static void ring(void *r, int32_t x, int32_t y, int32_t rad, int32_t w, const uint8_t *c)   /* octagon outline */
{
    static const int8_t px[9] = { 10, 7, 0, -7, -10, -7, 0, 7, 10 }, py[9] = { 0, 7, 10, 7, 0, -7, -10, -7, 0 };
    for (int i = 0; i < 8; i++)
        sq_line(r, x + px[i] * rad / 10, y + py[i] * rad / 10, x + px[i + 1] * rad / 10, y + py[i + 1] * rad / 10, w, c);
}

static void tri(void *r, int32_t x, int dir)   /* equilateral triangle outline, 8 px side on the line at x, pointing dir */
{
    int32_t y0 = LANE_Y(0), y1 = LANE_Y(0) + OP1_Q10(8), tx = x + dir * OP1_Q10(6.93), ty = LANE_Y(0) + OP1_Q10(4);
    sq_line(r, x, y0, tx, ty, 0x300, c_white);
    sq_line(r, tx, ty, x, y1, 0x300, c_white);
}
void ms_draw(void *r, const ms_view *v, ms_t *s)
{
    int32_t ph = v->ph, k = (int32_t)(((int64_t)v->zoom_q10 << 16) / 44100);   /* Q10 px per sample << 16 */
    int32_t span = (int32_t)(((int64_t)OP1_Q10(160) << 16) / (k ? k : 1));   /* samples to the screen edge */
#define MS_X(p) (OP1_Q10(160) + (int32_t)(((int64_t)((p) - ph) * k) >> 16))
#define ON(x) ((x) >= X0 && (x) <= X1)
#define TAPE_END 0xF23FA0                   /* 6 min of tape (docs/11), position units */
    int32_t beat = (int32_t)(((int64_t)2646000 << 8) / (v->bpm_q8 > 0 ? v->bpm_q8 : 120 << 8)), bar = beat * 4;

    /* status bar (y 3..17): selected track number in a square of the bar's height (grey while muted), position
     * bar.beat.sixteenth (Ableton style, 1.1.1 at tape 0) left, time m:ss.hh centred, input -> output right (I
     * instrument, S sequencer, M<channel> MIDI; input: what is recorded, output: where it plays). Stock font. */
    {
        int st = v->sel_track & 3, mu = s->h.mute[st];
        const uint8_t *cb = mu ? c_grey : c_white;
        sq_line(r, OP1_Q10(3), OP1_Q10(1), OP1_Q10(23), OP1_Q10(1), 0x500, cb);
        sq_line(r, OP1_Q10(23), OP1_Q10(1), OP1_Q10(23), OP1_Q10(21), 0x500, cb);
        sq_line(r, OP1_Q10(23), OP1_Q10(21), OP1_Q10(3), OP1_Q10(21), 0x500, cb);
        sq_line(r, OP1_Q10(3), OP1_Q10(21), OP1_Q10(3), OP1_Q10(1), 0x500, cb);
        char d[2] = { (char)('1' + st), 0 };
        txt(r, &tx[mu ? T_TRACK_G : T_TRACK_W], OP1_Q10(13), TXT_Y, cb, 1, d);   /* (centred in the square) */
        char t[16], *o = t;
        int32_t p = ph < 0 ? 0 : ph, b = p / bar, q = (p - b * bar) / beat, x = (p - b * bar - q * beat) / (beat / 4);
        o = sq_num(o, (uint32_t)b + 1, 0); *o++ = '.'; o = sq_num(o, (uint32_t)q + 1, 0); *o++ = '.'; sq_num(o, (uint32_t)x + 1, 0);
        txt(r, &tx[T_POS], OP1_Q10(30), TXT_Y, c_white, 0, t);
        static const char *const qn[MS_Q_N] = { "", "q4", "q8", "q16", "q32", "q8t", "q16t" };   /* (stock font: caps) */
        if (v->quant > 0 && v->quant < MS_Q_N) {                 /* after the position text (measured), gap 10 px */
            ms_txt *q = &tx[T_Q];
            int32_t qx = OP1_Q10(30) + txt_w(&tx[T_POS]) + OP1_Q10(10);
            if (qx < OP1_Q10(QUANT_X)) qx = OP1_Q10(QUANT_X);     /* (no jumping while the position is short) */
            if (q->made && ((int32_t *)q->mem)[0x58 / 4] != qx) { ((int32_t *)q->mem)[0x58 / 4] = qx; q->last[0] = 1; q->last[1] = 0; op1_text_set(q->mem, ""); }
            txt(r, q, qx, TXT_Y, c_grey, 0, qn[v->quant]);
        }
        uint32_t cs = (uint32_t)((int64_t)p * 100 / 44100);
        /* time: one text per digit in fixed cells (the stock font is proportional, a single string jumps) */
        sq_num(t, cs / 6000, 0);
        txt(r, &tx[T_MIN], OP1_Q10(TIME_X - 15), TXT_Y, c_white, 2, t);
        txt(r, &tx[T_COLON], OP1_Q10(TIME_X - 13), TXT_Y, c_white, 1, ":");
        char dg[2] = { 0, 0 }; uint32_t sec = cs / 100 % 60, hs = cs % 100;
        dg[0] = (char)('0' + sec / 10); txt(r, &tx[T_S1], OP1_Q10(TIME_X - 7), TXT_Y, c_white, 1, dg);
        dg[0] = (char)('0' + sec % 10); txt(r, &tx[T_S2], OP1_Q10(TIME_X), TXT_Y, c_white, 1, dg);
        txt(r, &tx[T_DOT], OP1_Q10(TIME_X + 5), TXT_Y, c_white, 1, ".");
        dg[0] = (char)('0' + hs / 10); txt(r, &tx[T_H1], OP1_Q10(TIME_X + 10), TXT_Y, c_white, 1, dg);
        dg[0] = (char)('0' + hs % 10); txt(r, &tx[T_H2], OP1_Q10(TIME_X + 17), TXT_Y, c_white, 1, dg);
        static const char *const dl[3] = { "i", "s", "m" };   /* (the stock font draws a-z as capitals) */
        int ik = s->h.in[st] & 15, ok = s->h.dest[st] % 3;
        o = sq_cat(t, dl[ik % 3]);
        if (ik == MS_DEST_MIDI) sq_num(o, (s->h.in[st] >> 4) + 1u, 0);
        txt(r, &tx[v->io ? T_IN_W : T_IN_G], OP1_Q10(ARROW_X - 4), TXT_Y, v->io ? c_white : c_grey, 2, t);   /* white: the white knob edits it, grey: the other */
        int32_t ax = OP1_Q10(ARROW_X), ay = OP1_Q10(10);              /* arrow */
        sq_line(r, ax, ay, ax + OP1_Q10(9), ay, 0x500, c_grey);
        sq_line(r, ax + OP1_Q10(5), ay - OP1_Q10(3.5), ax + OP1_Q10(9), ay, 0x500, c_grey);
        sq_line(r, ax + OP1_Q10(5), ay + OP1_Q10(3.5), ax + OP1_Q10(9), ay, 0x500, c_grey);
        o = sq_cat(t, dl[ok]);
        if (ok == MS_DEST_MIDI) sq_num(o, s->h.ch[st] + 1u, 0);
        txt(r, &tx[v->io ? T_OUT_G : T_OUT_W], ax + OP1_Q10(13), TXT_Y, v->io ? c_grey : c_white, 0, t);
    }

    /* bars and beats: lines through the lanes (bars thicker), beat ticks above the lanes when there is room */
    int32_t bpx = (int32_t)(((int64_t)bar * k) >> 16);
    int32_t b0 = ph - span > 0 ? (ph - span) / bar : 0;
    for (int32_t bi = b0, n = 0; n < 64; bi++, n++) {
        int32_t xb = MS_X(bi * bar);
        if (xb > X1) break;
        if (ON(xb)) sq_line(r, xb, LANE_Y(0), xb, LANE_Y(4), 0x500, c_bar);
        if (bpx >= OP1_Q10(32))
            for (int q = 1; q < 4; q++) {
                int32_t xq = MS_X(bi * bar + q * beat);
                if (ON(xq)) sq_line(r, xq, LANE_Y(0) - OP1_Q10(3), xq, LANE_Y(0), 0x300, c_bar);
            }
    }
    /* the tape's extent on screen (bar 1 .. 6 min): the track separators stop there, continuous while scrolling */
    int32_t mlo = MS_X(0), mhi = MS_X(TAPE_END);
    if (mlo < X0) mlo = X0;
    if (mhi > X1) mhi = X1;

    /* loop (as the tape: green bar, open ring at loop in, filled dot at loop out) + green lines through the lanes */
    if (v->loop_on && v->loop_out > v->loop_in) {
        int32_t xi = MS_X(v->loop_in), xo = MS_X(v->loop_out), a = xi < X0 ? X0 : xi, b = xo > X1 ? X1 : xo;
        if (b > a) sq_line(r, a, BAND_Y, b, BAND_Y, 0x800, c_green);
        if (ON(xi)) { sq_line(r, xi, LANE_Y(0), xi, LANE_Y(4), 0x700, c_green); ring(r, xi, BAND_Y, OP1_Q10(3), 0x500, c_green); }
        if (ON(xo)) { sq_line(r, xo, LANE_Y(0), xo, LANE_Y(4), 0x700, c_green); sq_line(r, xo - OP1_Q10(3), BAND_Y, xo + OP1_Q10(3), BAND_Y, OP1_Q10(6), c_green); }
    }
    else if (v->loop_in >= 0) {                          /* loop off: the start marker stays (as the tape's ring) */
        int32_t xi = MS_X(v->loop_in);
        if (ON(xi)) ring(r, xi, BAND_Y, OP1_Q10(3), 0x500, c_green);
    }

    /* tape markers (as tape/markers.c: caret v, white within 3 px of the playhead, red while punch mode is on; grey
     * line through the tracks) */
    int32_t tol = v->zoom_q10 > 0 ? (int32_t)((3LL * 1024 * 44100) / v->zoom_q10) : 0;
    for (int i = 0; i < v->tm_n; i++) {
        int32_t xm = MS_X(v->tm[i]);
        if (!ON(xm)) continue;
        int32_t d = v->tm[i] > ph ? v->tm[i] - ph : ph - v->tm[i];
        const uint8_t *cm = d <= tol ? c_white : v->punch ? c_red : c_green;
        sq_line(r, xm, LANE_Y(0), xm, LANE_Y(4), 0x400, c_grey);
        sq_line(r, xm - OP1_Q10(3), BAND_Y - OP1_Q10(7), xm, BAND_Y - OP1_Q10(2), 0x600, cm);
        sq_line(r, xm + OP1_Q10(3), BAND_Y - OP1_Q10(7), xm, BAND_Y - OP1_Q10(2), 0x600, cm);
    }

    /* tracks: separator, notes (pitch inside the lane), CCs */
    static ms_ev ev[96]; static int32_t ps[96];
#define SEL(p) (in_sel && (p) >= v->sel_a && (p) < v->sel_b)
    for (int t = 0; t < MS_TRACKS; t++) {
        int32_t y = LANE_Y(t);
        const uint8_t *lc = t == v->sel_track ? c_red : s->h.mute[t] ? c_grey : c_white;
        int in_sel = v->sel_b >= 0 && (t == v->sel_track || v->shift);   /* LIFT takes these: blue */
        if (mhi > mlo) sq_line(r, mlo, y + OP1_Q10(26), mhi, y + OP1_Q10(26), 0x600, c_lane);   /* within the tape */
        int n = ms_events(s, t, ph - span, ph + span, ev, ps, 96);
        for (int i = 0; i < n; i++) {
            int32_t x0 = MS_X(ps[i]);
            if (!ON(x0)) continue;
            if (ev[i].kind == MS_CC) {
                int32_t yy = y + OP1_Q10(24) - ev[i].d2 * OP1_Q10(20) / 127;
                sq_line(r, x0, yy, x0 + OP1_Q10(1), yy, 0x400, (ps[i] >= ph - 64 && ps[i] <= ph) || SEL(ps[i]) ? c_blue : lc);
                continue;
            }
            int32_t x1 = x0 + (int32_t)(((int64_t)ev[i].len * 64 * k) >> 16);
            if (x1 < x0 + OP1_Q10(1)) x1 = x0 + OP1_Q10(1);
            if (x1 > X1) x1 = X1;
            int32_t yy = y + OP1_Q10(23) - (ev[i].d1 % 24) * OP1_Q10(20) / 23;
            int under = ps[i] <= ph && ph < ps[i] + (int32_t)(ev[i].len ? ev[i].len : 1) * 64;   /* sounding at the cursor */
            sq_line(r, x0, yy, x1, yy, 0x600, under || SEL(ps[i]) ? c_blue : lc);
        }
    }

    /* selection (white lines, a triangle at the top pointing into it), playhead (red while recording) */
    if (v->sel_a >= 0) {
        int32_t xa = MS_X(v->sel_a);
        if (ON(xa)) { sq_line(r, xa, LANE_Y(0), xa, LANE_Y(4), 0x400, c_white); tri(r, xa, 1); }
        if (v->sel_b >= 0) { int32_t xb = MS_X(v->sel_b); if (ON(xb)) { sq_line(r, xb, LANE_Y(0), xb, LANE_Y(4), 0x400, c_white); tri(r, xb, -1); } }
    }
    sq_line(r, OP1_Q10(160), LANE_Y(0) - OP1_Q10(4), OP1_Q10(160), LANE_Y(4) + OP1_Q10(2), 0x400, v->rec ? c_red : c_white);
    (void)c_dim;
#undef MS_X
#undef SEL
#undef ON
}
