/* Preview frames of the MIDI sequencer view (seqdev/ms_draw.c) for render_mseq.py: a song with notes and CCs on
 * the tape timeline, frames with the loop, markers, punch mode, recording, a selection. Prints one JSON frame per
 * line ({"label", "lines": [[x0, y0, x1, y1, w, rgba]...]}), as the other preview programs. */
#include "../op1_synth.h"
#include "../seqdev/ms_draw.h"
#include <stdio.h>
#include <string.h>
#define MAXL 4000
static struct { int32_t x0, y0, x1, y1, w; uint8_t c[4]; } lines[MAXL]; static int nlines;
void *host_raster(void) { return &nlines; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c)
{ (void)r; const op1_pt *p = a, *q = b; if (nlines < MAXL) { lines[nlines].x0 = p->x; lines[nlines].y0 = p->y; lines[nlines].x1 = q->x; lines[nlines].y1 = q->y; lines[nlines].w = w; memcpy(lines[nlines].c, c, 4); nlines++; } }
typedef struct { int32_t x, y; uint8_t c[4]; int align; char s[32]; } txt_t;
static txt_t *drawn[64]; static int nt;
void *host_text_ctor(void *o, int32_t x, int32_t y, int32_t sz, const uint8_t *c)
{ txt_t *t = o; (void)sz; t->x = x; t->y = y; memcpy(t->c, c, 4); t->s[0] = 0; t->align = 0; return o; }
void host_text_align(void *o, int a) { ((txt_t *)o)->align = a; }
void host_text_set(void *o, const char *s) { snprintf(((txt_t *)o)->s, 32, "%s", s); }
int32_t host_text_width(const void *o) { return (int32_t)strlen(((const txt_t *)o)->s) * 7 * 1024; }
void host_text_draw(void *o, void *r, const int32_t *m) { (void)r; (void)m; if (nt < 64) drawn[nt++] = o; }
static int32_t pos; static int playing;
static int32_t e_pos(void) { return pos; }
static int e_playing(void) { return playing; }
static int e_loop(int32_t *a, int32_t *b) { *a = *b = 0; return 0; }
static void e_play(int t, int d, int ch, int k, int d1, int d2, int on) { (void)t; (void)d; (void)ch; (void)k; (void)d1; (void)d2; (void)on; }
static int e_rs(int s, ms_slot *o) { (void)s; (void)o; return -1; }
static int e_ws(int s, const ms_slot *i) { (void)s; (void)i; return 0; }
static int e_rh(void *h, int n) { (void)h; (void)n; return -1; }
static int e_wh(const void *h, int n) { (void)h; (void)n; return 0; }
static uint32_t e_off(void) { return 0; }
static void e_on(uint32_t m) { (void)m; }
static const ms_env env = { e_pos, e_playing, e_loop, e_play, e_rs, e_ws, e_rh, e_wh, e_off, e_on };
static ms_t S;
static void frame(const char *label, const ms_view *v)
{
    nlines = 0; nt = 0;
    ms_draw(&nlines, v, &S);
    printf("{\"label\": \"%s\", \"nodes\": {}, \"lines\": [", label);
    for (int i = 0; i < nlines; i++) printf("%s[%d, %d, %d, %d, %d, [%d, %d, %d, %d]]", i ? ", " : "", lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1, lines[i].w, lines[i].c[0], lines[i].c[1], lines[i].c[2], lines[i].c[3]);
    printf("], \"text\": [");
    for (int i = 0; i < nt; i++) printf("%s[%d, %d, [%d, %d, %d], \"%s\", %d]", i ? ", " : "", drawn[i]->x, drawn[i]->y,
                                        drawn[i]->c[0], drawn[i]->c[1], drawn[i]->c[2], drawn[i]->s, drawn[i]->align);
    printf("]}\n");
}
static void note(int t, int32_t at, int n, int len_ms)
{
    S.sel = t; S.rec = 1; playing = 1; pos = at; S.prev = at;
    ms_note_in(&S, n, 100, 0, 1);
    S.prev = at + len_ms * 441 / 10; ms_note_in(&S, n, 0, 0, 0);
}
int main(void)
{
    ms_init(&S, &env);
    int32_t beat = 22050;                                       /* 120 bpm */
    for (int i = 0; i < 32; i++) note(0, 44100 + i * beat / 2, 48 + (i * 5) % 24, 180);      /* bass line, 8ths */
    for (int i = 0; i < 8; i++) { note(1, 44100 + i * beat * 2, 60 + (i % 3) * 4, 900); note(1, 44100 + i * beat * 2, 67, 900); }   /* chords */
    for (int i = 0; i < 64; i++) { S.sel = 2; S.prev = 44100 + i * beat / 4; S.cc_last[74] = -10000; ms_cc_in(&S, 74, (i * 8) % 128, 0); }   /* CC sweep */
    for (int i = 0; i < 16; i++) note(3, 44100 + i * beat, i % 4 == 0 ? 36 : 42, 60);        /* drums */
    S.rec = 0; S.h.dest[1] = MS_DEST_MIDI; S.h.ch[1] = 1; S.h.dest[3] = MS_DEST_SEQ; S.h.mute[2] = 0;
    S.h.in[2] = MS_DEST_MIDI | 4 << 4;                          /* track 3: input MIDI channel 5 */
    static const int32_t tm[3] = { 66150, 132300, 220500 };
    ms_view v = { 0 };
    v.ph = 110250; v.zoom_q10 = 40 << 10; v.bpm_q8 = 120 << 8; v.loop_on = 1; v.loop_in = 88200; v.loop_out = 176400;
    v.sel_a = v.sel_b = -1; v.sel_track = 0; v.tm_n = 3; v.tm = tm;
    frame("loop, markers, playhead at 2.2.1", &v);
    v.ph = 132300; frame("on a marker (white)", &v);
    v.ph = 110250; v.punch = 1; v.rec = 1; frame("recording, punch mode (red markers)", &v);
    v.punch = 0; v.rec = 0; v.sel_a = 99225; v.sel_b = 154350; v.sel_track = 2; frame("selection, track 3 (input M5)", &v);
    v.shift = 1; frame("selection, Shift held (all tracks)", &v); v.shift = 0;
    v.sel_a = v.sel_b = -1; v.sel_track = 1; frame("track 2 (I -> M2), output selected", &v);
    v.io = 1; frame("track 2, input selected (white press)", &v); v.io = 0; v.sel_track = 2;
    v.zoom_q10 = 12 << 10; v.sel_a = v.sel_b = -1; v.sel_track = 0; frame("zoomed out", &v);
    v.zoom_q10 = 40 << 10; v.loop_on = 0; frame("loop off: start marker only", &v);
    v.quant = MS_Q_16; frame("quantize 1/16 (Shift + blue)", &v); v.quant = MS_Q_8T; frame("quantize 1/8T", &v);
    return 0;
}
