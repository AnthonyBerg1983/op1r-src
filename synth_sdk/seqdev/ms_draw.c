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
static void txt(void *r, ms_txt *t, int32_t x, int32_t y, const uint8_t *c, int align, const char *str)
{
    if (!t->made) { TXT_CTOR(t->mem, x, y, c, align); t->made = 1; t->last[0] = 1; t->last[1] = 0; }
    int k = 0;
    while (k < 11 && str[k] && str[k] == t->last[k]) k++;
    if (str[k] != t->last[k]) { op1_text_set(t->mem, str); for (k = 0; k < 11 && (t->last[k] = str[k]) != 0; k++) ; t->last[11] = 0; }
    op1_text_draw(t->mem, r, ident);
}
enum { T_TRACK_W, T_TRACK_G, T_POS, T_TIME, T_IN, T_OUT, T_N };
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
#define ARROW_X 280                          /* input -> output arrow, px */

static void ring(void *r, int32_t x, int32_t y, int32_t rad, int32_t w, const uint8_t *c)   /* octagon outline */
{
    static const int8_t px[9] = { 10, 7, 0, -7, -10, -7, 0, 7, 10 }, py[9] = { 0, 7, 10, 7, 0, -7, -10, -7, 0 };
    for (int i = 0; i < 8; i++)
        sq_line(r, x + px[i] * rad / 10, y + py[i] * rad / 10, x + px[i + 1] * rad / 10, y + py[i + 1] * rad / 10, w, c);
}
static int32_t slen(const char *t) { int32_t n = 0; while (t[n]) n++; return n; }
static char *two(char *o, uint32_t v) { *o++ = (char)('0' + v / 10 % 10); *o++ = (char)('0' + v % 10); *o = 0; return o; }

void ms_draw(void *r, const ms_view *v, ms_t *s)
{
    int32_t ph = v->ph, k = (int32_t)(((int64_t)v->zoom_q10 << 16) / 44100);   /* Q10 px per sample << 16 */
    int32_t span = (int32_t)(((int64_t)OP1_Q10(160) << 16) / (k ? k : 1));   /* samples to the screen edge */
#define MS_X(p) (OP1_Q10(160) + (int32_t)(((int64_t)((p) - ph) * k) >> 16))
#define ON(x) ((x) >= X0 && (x) <= X1)
    int32_t beat = (int32_t)(((int64_t)2646000 << 8) / (v->bpm_q8 > 0 ? v->bpm_q8 : 120 << 8)), bar = beat * 4;

    /* status bar (y 3..17): selected track number in a square of the bar's height (grey while muted), position
     * bar.beat.sixteenth (Ableton style, 1.1.1 at tape 0) left, time m:ss.hh centred, input -> output right (I
     * instrument, S sequencer, M<channel> MIDI; the input is a placeholder, not used yet). Stock font. */
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
        uint32_t cs = (uint32_t)((int64_t)p * 100 / 44100);
        o = t; o = sq_num(o, cs / 6000, 0); *o++ = ':'; o = two(o, cs / 100 % 60); *o++ = '.'; two(o, cs % 100);
        txt(r, &tx[T_TIME], OP1_Q10(160), TXT_Y, c_white, 1, t);
        static const char *const dl[3] = { "i", "s", "m" };   /* (the stock font draws a-z as capitals) */
        int ik = s->h.in[st] & 15, ok = s->h.dest[st] % 3;
        o = sq_cat(t, dl[ik % 3]);
        if (ik == MS_DEST_MIDI) sq_num(o, (s->h.in[st] >> 4) + 1u, 0);
        txt(r, &tx[T_IN], OP1_Q10(ARROW_X - 4), TXT_Y, c_white, 2, t);
        int32_t ul = OP1_Q10(19);                                     /* white knob edits the underlined side */
        if (v->io) sq_line(r, OP1_Q10(ARROW_X - 1) - OP1_Q10(7) * slen(t), ul, OP1_Q10(ARROW_X - 1), ul, 0x300, c_white);
        int32_t ax = OP1_Q10(ARROW_X), ay = OP1_Q10(10);              /* arrow */
        sq_line(r, ax, ay, ax + OP1_Q10(9), ay, 0x500, c_grey);
        sq_line(r, ax + OP1_Q10(5), ay - OP1_Q10(3.5), ax + OP1_Q10(9), ay, 0x500, c_grey);
        sq_line(r, ax + OP1_Q10(5), ay + OP1_Q10(3.5), ax + OP1_Q10(9), ay, 0x500, c_grey);
        o = sq_cat(t, dl[ok]);
        if (ok == MS_DEST_MIDI) sq_num(o, s->h.ch[st] + 1u, 0);
        txt(r, &tx[T_OUT], ax + OP1_Q10(13), TXT_Y, c_white, 0, t);
        if (!v->io) sq_line(r, ax + OP1_Q10(13), ul, ax + OP1_Q10(13) + OP1_Q10(7) * slen(t), ul, 0x300, c_white);
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

    /* loop (as the tape: green bar, open ring at loop in, filled dot at loop out) + green lines through the lanes */
    if (v->loop_on && v->loop_out > v->loop_in) {
        int32_t xi = MS_X(v->loop_in), xo = MS_X(v->loop_out), a = xi < X0 ? X0 : xi, b = xo > X1 ? X1 : xo;
        if (b > a) sq_line(r, a, BAND_Y, b, BAND_Y, 0x800, c_green);
        if (ON(xi)) { sq_line(r, xi, LANE_Y(0), xi, LANE_Y(4), 0x700, c_green); ring(r, xi, BAND_Y, OP1_Q10(3), 0x500, c_green); }
        if (ON(xo)) { sq_line(r, xo, LANE_Y(0), xo, LANE_Y(4), 0x700, c_green); sq_line(r, xo - OP1_Q10(3), BAND_Y, xo + OP1_Q10(3), BAND_Y, OP1_Q10(6), c_green); }
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
    for (int t = 0; t < MS_TRACKS; t++) {
        int32_t y = LANE_Y(t);
        const uint8_t *lc = t == v->sel_track ? c_red : s->h.mute[t] ? c_grey : c_white;
        sq_line(r, X0, y + OP1_Q10(26), X1, y + OP1_Q10(26), 0x600, c_lane);
        int n = ms_events(s, t, ph - span, ph + span, ev, ps, 96);
        for (int i = 0; i < n; i++) {
            int32_t x0 = MS_X(ps[i]);
            if (!ON(x0)) continue;
            if (ev[i].kind == MS_CC) {
                int32_t yy = y + OP1_Q10(24) - ev[i].d2 * OP1_Q10(20) / 127;
                sq_line(r, x0, yy, x0 + OP1_Q10(1), yy, 0x400, ps[i] >= ph - 64 && ps[i] <= ph ? c_blue : lc);
                continue;
            }
            int32_t x1 = x0 + (int32_t)(((int64_t)ev[i].len * 64 * k) >> 16);
            if (x1 < x0 + OP1_Q10(1)) x1 = x0 + OP1_Q10(1);
            if (x1 > X1) x1 = X1;
            int32_t yy = y + OP1_Q10(23) - (ev[i].d1 % 24) * OP1_Q10(20) / 23;
            int under = ps[i] <= ph && ph < ps[i] + (int32_t)(ev[i].len ? ev[i].len : 1) * 64;   /* sounding at the cursor */
            sq_line(r, x0, yy, x1, yy, 0x600, under ? c_blue : lc);
        }
    }

    /* selection (white), playhead (red while recording) */
    if (v->sel_a >= 0) {
        int32_t xa = MS_X(v->sel_a);
        if (ON(xa)) sq_line(r, xa, LANE_Y(0), xa, LANE_Y(4), 0x400, c_white);
        if (v->sel_b >= 0) { int32_t xb = MS_X(v->sel_b); if (ON(xb)) sq_line(r, xb, LANE_Y(0), xb, LANE_Y(4), 0x400, c_white); }
    }
    sq_line(r, OP1_Q10(160), LANE_Y(0) - OP1_Q10(4), OP1_Q10(160), LANE_Y(4) + OP1_Q10(2), 0x400, v->rec ? c_red : c_white);
    (void)c_dim;
#undef MS_X
#undef ON
}
