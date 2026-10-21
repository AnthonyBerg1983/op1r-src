/* Host test + preview frames for screens/fxroute.c (master FX source, docs/54): capture / route math, values, saved.
 * "render": JSON frames for harness/render_usbout.py */
#include "../op1_synth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../screens/fxroute.c"

static struct { int32_t x0, y0, x1, y1, w; uint8_t c[4]; } L[256]; static int nl;
typedef struct { int32_t x, y; uint8_t c[4]; char s[32]; } txt_t;
static txt_t *drawn[16]; static int nt;
void host_raster_line(void *r, const void *p0, const void *p1, int32_t w, const uint8_t *c)
{ (void)r; const op1_pt *a = p0, *b = p1; L[nl].x0 = a->x; L[nl].y0 = a->y; L[nl].x1 = b->x; L[nl].y1 = b->y; L[nl].w = w; memcpy(L[nl].c, c, 4); nl++; }
void *host_text_ctor(void *o, int32_t x, int32_t y, int32_t sz, const uint8_t *c)
{ txt_t *t = o; (void)sz; t->x = x; t->y = y; memcpy(t->c, c, 4); t->s[0] = 0; return o; }
void host_text_set(void *o, const char *s) { snprintf(((txt_t *)o)->s, 32, "%s", s); }
void host_text_draw(void *o, void *r, const int32_t *m) { (void)r; (void)m; drawn[nt++] = o; }
void *host_raster(void) { return (void *)1; }
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; }

static void frame(const char *label, int render)
{
    nl = nt = 0; fr_update(0, 1);
    if (!render) return;
    printf("{\"label\": \"%s\", \"lines\": [", label);
    for (int i = 0; i < nl; i++) printf("%s[%d,%d,%d,%d,%d,[%d,%d,%d,%d]]", i ? "," : "", L[i].x0, L[i].y0, L[i].x1, L[i].y1, L[i].w, L[i].c[0], L[i].c[1], L[i].c[2], L[i].c[3]);
    printf("], \"text\": [");
    for (int i = 0; i < nt; i++) printf("%s[%d,%d,[%d,%d,%d],\"%s\"]", i ? "," : "", drawn[i]->x, drawn[i]->y, drawn[i]->c[0], drawn[i]->c[1], drawn[i]->c[2], drawn[i]->s);
    printf("]}\n");
}

static void fx_gain2(void *s, int16_t *l, int16_t *r, const int16_t *p) { (void)s; (void)p; for (int i = 0; i < 64; i++) { l[i] *= 2; r[i] *= 2; } }

int main(int argc, char **argv)
{
    int render = argc > 1 && !strcmp(argv[1], "render");
    fr_init(0); assert(fr_src == 0);
    frame("master (default)", render);
    int16_t l[64], r[64];
    for (int i = 0; i < 64; i++) { l[i] = 100; r[i] = 50; }
    fr_route(fx_gain2, 0, l, r, 0); assert(l[0] == 200 && r[0] == 100);          /* master: whole mix */
    fxroute_turn(0, 1024); assert(fr_src == 1 && fr_host_saved == 1);
    frame("blue right: instrument", render);
    for (int i = 0; i < 64; i++) { fr_host_tape[0][i] = 1000; fr_host_tape[1][i] = -300; }   /* tape alone */
    fr_snap();
    for (int i = 0; i < 64; i++) { fr_host_tape[0][i] += 40; fr_host_tape[1][i] += 10; }      /* + instrument */
    fr_capture();
    for (int i = 0; i < 64; i++) { l[i] = fr_host_tape[0][i]; r[i] = fr_host_tape[1][i]; }
    fr_route(fx_gain2, 0, l, r, 0); assert(l[5] == 1080 && r[5] == -280);         /* only the instrument doubled */
    fr_route(fx_gain2, 0, l, r, 0); assert(l[5] == 2160);                          /* no fresh capture: whole mix */
    fxroute_turn(1, 1024); assert(fr_out == 1 && fr_host_saved == 3);
    frame("green right: mono tape", render);
    for (int i = 0; i < 64; i++) { fr_host_bus[i] = (int16_t)(i * 100 - 3000); fr_host_tape[0][i] = 500; fr_host_tape[1][i] = 500; }
    fr_snap();
    for (int i = 0; i < 64; i++) { fr_host_tape[0][i] += fr_host_bus[i] / 2; fr_host_tape[1][i] += fr_host_bus[i] / 4; }   /* gains 0.5 / 0.25 */
    fr_capture();
    for (int i = 0; i < 64; i++) { l[i] = fr_host_tape[0][i]; r[i] = fr_host_tape[1][i]; }
    int16_t bus0[64]; memcpy(bus0, fr_host_bus, sizeof bus0);
    fr_route(fx_gain2, 0, l, r, 0);
    for (int i = 0; i < 64; i++) {
        assert(fr_host_bus[i] == 2 * bus0[i]);                                      /* recorded: wet mono */
        int el = 500 + bus0[i], er = 500 + bus0[i] / 2;                             /* monitor: wet x track gain */
        assert(abs(l[i] - el) <= 2 && abs(r[i] - er) <= 2);
    }
    fxroute_turn(0, -1024); fxroute_turn(1, -1024); assert(fr_out == 1);            /* green: instrument only */
    fxroute_turn(0, 1024); fxroute_turn(1, -1024); assert(fr_out == 0 && fr_host_saved == 1);
    fxroute_turn(0, 1024); assert(fr_src == 1);                                    /* clamped */
    fxroute_turn(1, -4096); assert(fr_src == 1);
    fxroute_turn(0, -1024); assert(fr_src == 0 && fr_host_saved == 0);
    fr_host_saved = 1; fr_init(0); assert(fr_src == 1);                            /* loaded */
    if (!render) printf("ok\n");
    return 0;
}
