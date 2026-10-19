/* Preview frames of the chord page (chord/ch_fw.c screen) for render_chord.py, one JSON frame per line, plus a
 * firmware-glue test: key down / up through ch_key_down / ch_key_up (strum on ticks, drum pads, shared notes).
 *   cc -DOP1_HOST -I.. chord_render.c ../chord/ch_fw.c ../chord/ch_core.c -o build/chord_render && build/chord_render */
#include "../op1_synth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int  ch_key_down(int m, int key);
void ch_key_up(int m, int key);
void ch_tick(int m);
void ch_turn(int m, int knob, int delta, int shift);
void ch_press(int m, int knob);
void ch_host_draw(void *r, int m);
#define MAXL 256
static struct { int32_t x0, y0, x1, y1, w; uint8_t c[4]; } lines[MAXL]; static int nlines;
void *host_raster(void) { return &nlines; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c)
{
    (void)r; const op1_pt *p = a, *q = b;
    assert(nlines < MAXL);
    lines[nlines].x0 = p->x; lines[nlines].y0 = p->y; lines[nlines].x1 = q->x; lines[nlines].y1 = q->y; lines[nlines].w = w;
    memcpy(lines[nlines].c, c, 4); nlines++;
}
typedef struct { int32_t x, y, sz; uint8_t c[4]; char s[32]; } txt_t;
static txt_t *drawn[32]; static int nt;
void *host_text_ctor(void *o, int32_t x, int32_t y, int32_t sz, const uint8_t *c)
{ txt_t *t = o; t->x = x; t->y = y; t->sz = sz; memcpy(t->c, c, 4); t->s[0] = 0; return o; }
void host_text_set(void *o, const char *s) { snprintf(((txt_t *)o)->s, 32, "%s", s); }
void host_text_draw(void *o, void *r, const int32_t *m) { (void)r; (void)m; if (nt < 32) drawn[nt++] = o; }
int sq_join(char *out, int n, const int *v, int k) { (void)out; (void)n; (void)v; return k; }
int sq_split(const char *s, int len, int *v, int k) { (void)s; (void)len; (void)v; return k; }
static char nlog[512];
void host_ch_note(int pos, int note, int vel, int on) { (void)vel; sprintf(nlog + strlen(nlog), "%s%c%d ", pos ? "d" : "", on ? '+' : '-', note); }

static void frame(const char *label, int m)
{
    nlines = 0; nt = 0;
    ch_host_draw(&nlines, m);
    printf("{\"label\": \"%s\", \"lines\": [", label);
    for (int i = 0; i < nlines; i++)
        printf("%s[%d, %d, %d, %d, %d, [%d, %d, %d, %d]]", i ? ", " : "", lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1,
               lines[i].w, lines[i].c[0], lines[i].c[1], lines[i].c[2], lines[i].c[3]);
    printf("], \"text\": [");
    for (int i = 0; i < nt; i++)
        printf("%s[%d, %d, [%d, %d, %d], \"%s\", 0, %d]", i ? ", " : "", drawn[i]->x, drawn[i]->y, drawn[i]->c[0], drawn[i]->c[1],
               drawn[i]->c[2], drawn[i]->s, drawn[i]->sz);
    printf("]}\n");
}
static void turn(int m, int knob, int detents) { ch_turn(m, knob, detents * 1024, 0); }

int main(int argc, char **argv)
{
    /* glue: C (key 7, midi 60) major, no strum: three notes at once; up releases them */
    nlog[0] = 0; ch_key_down(0, 7); assert(!strcmp(nlog, "+60 +64 +67 ")); nlog[0] = 0; ch_key_up(0, 7); assert(!strcmp(nlog, "-60 -64 -67 "));
    /* strum 1/48 (2 ticks): first note at once, the rest on the ticks; key up cancels the rest */
    turn(0, 2, 2);
    nlog[0] = 0; ch_key_down(0, 7); assert(!strcmp(nlog, "+60 "));
    ch_tick(0); assert(!strcmp(nlog, "+60 ")); ch_tick(0); assert(!strcmp(nlog, "+60 +64 "));
    nlog[0] = 0; ch_key_up(0, 7); assert(!strcmp(nlog, "-60 -64 "));
    ch_tick(0); ch_tick(0); ch_tick(0); assert(!strcmp(nlog, "-60 -64 "));
    turn(0, 2, -2);
    /* drum mode: key 0 + major -> pads 0, 4, 7 (notes 0x35 + pad) */
    nlog[0] = 0; ch_key_down(1, 0); assert(!strcmp(nlog, "d+53 d+57 d+60 ")); ch_key_up(1, 0);
    if (argc > 1) return 0;                                    /* (test only) */
    frame("fixed maj, C played", 0);
    turn(0, 0, 6); turn(0, 1, 1); ch_press(0, 1); ch_key_down(0, 9); ch_key_up(0, 9);
    frame("min7, inv 1 + bass, D played", 0);
    ch_press(0, 0); turn(0, 0, 1); turn(0, 3, 9); ch_press(0, 3); turn(0, 2, 3); ch_key_down(0, 14);
    frame("scale 7th, A minor, strum 1/32 up, G played (held)", 0);
    return 0;
}
