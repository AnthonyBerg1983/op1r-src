/* Preview frames of the grids sequencer page (grids/gr_ui.c) for render_grids.py, one JSON frame per line.
 *   cc -DOP1_HOST -I.. grids_render.c ../grids/gr_ui.c ../grids/gr_core.c -o build/grids_render */
#include "../op1_synth.h"
#include "../grids/gr_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAXL 400
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

static void frame(const char *label, int m, int shift)
{
    nlines = 0; nt = 0;
    gr_ui_draw(&nlines, m, shift);
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
static void play(int m, int steps) { for (int t = 0; t < steps; t++) gr_ui_step(m, t % GR_STEPS); }

int main(void)
{
    gr_ui_init(0); gr_ui_init(1);
    play(1, 11); frame("drum mode, defaults (centre of the map, fill 50%), step 10", 1, 0);
    gr_ui_turn(1, 0, -32, 0); gr_ui_turn(1, 1, -32, 0);
    for (int i = 0; i < 3; i++) { gr_ui_turn(1, 2, 12, 0); gr_ui_press(1, 2); }
    play(1, 21); frame("corner x 0 / y 0, fills 69%, step 20", 1, 0);
    gr_ui_turn(1, 0, 40, 0); gr_ui_turn(1, 1, 20, 0); gr_ui_turn(1, 3, 30, 0); gr_ui_press(1, 2);
    gr_b[1].res32 = 1; play(1, 33); frame("x 63% y 31%, chaos 47%, 1/32 steps, sd selected", 1, 0);
    frame("Shift: swing, resolution, pad, accent", 1, 1);
    gr_ui_press(0, 3); gr_ui_turn(0, 2, 30, 0); play(0, 5); frame("synth mode, stopped, bd fill 97%", 0, 0);
    frame("synth mode, Shift (bd note c2)", 0, 1);
    return 0;
}
