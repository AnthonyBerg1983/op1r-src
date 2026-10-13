/* Host test + preview frames for screens/oblique.c. The Text mock lays out characters with the generator's width
 * model (build/oblique_hostw.h) and centres them like the firmware (alignment 1). "render": JSON per card. */
#include "../op1_synth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "build/oblique_hostw.h"
#include "../screens/oblique.c"

typedef struct { int32_t x, y, align, n; int32_t pos[OB_LEN]; char s[OB_LEN]; uint8_t c[4]; } htxt;
static htxt store[64]; static void *keys[64]; static int nk;
static htxt *T(void *o)                                     /* mock state beside the 0xA4-byte object */
{
    for (int i = 0; i < nk; i++) if (keys[i] == o) return &store[i];
    assert(nk < 64); keys[nk] = o; return &store[nk++];
}
void *host_text_ctor(void *o, int32_t x, int32_t y, int32_t sz, const uint8_t *c)
{ (void)sz; htxt *t = T(o); memset(t, 0, sizeof *t); t->x = x; t->y = y; memcpy(t->c, c, 4); return o; }
void host_text_align(void *o, int a) { T(o)->align = a; }
void host_text_set(void *o, const char *s)
{
    htxt *t = T(o); int x16 = 0, n = (int)strlen(s);
    for (int i = 0; i < n; i++) { t->pos[i] = x16; x16 += host_w16[(int)s[i] & 127] + (i + 1 < n ? HOST_GAP16 : 0); }
    int x0 = (t->x >> 10) * 16 - (t->align == 1 ? x16 / 2 : 0);
    for (int i = 0; i < n; i++) t->pos[i] = (x0 + t->pos[i]) / 16;
    t->n = n; snprintf(t->s, OB_LEN, "%s", s);
}
int32_t *host_text_pos(void *o, int32_t *n) { *n = T(o)->n; return T(o)->pos; }
static htxt *drawn[16]; static int nt;
void host_text_draw(void *o, void *r, const int32_t *m) { (void)r; (void)m; drawn[nt++] = T(o); }
static struct { int32_t x0, y0, x1, y1, w; uint8_t c[4]; } L[512]; static int nl;
void host_raster_line(void *r, const void *p0, const void *p1, int32_t w, const uint8_t *c)
{ (void)r; const op1_pt *a = p0, *b = p1; L[nl].x0 = a->x; L[nl].y0 = a->y; L[nl].x1 = b->x; L[nl].y1 = b->y; L[nl].w = w; memcpy(L[nl].c, c, 4); nl++; }
void *host_raster(void) { return (void *)1; }
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; }

static void frame(void)
{
    nl = nt = 0; ob_update(0, 1);
    printf("{\"lines\": [");
    for (int i = 0; i < nl; i++) printf("%s[%d,%d,%d,%d,%d,[%d,%d,%d,%d]]", i ? "," : "", L[i].x0, L[i].y0, L[i].x1, L[i].y1, L[i].w, L[i].c[0], L[i].c[1], L[i].c[2], L[i].c[3]);
    printf("], \"text\": [");
    for (int i = 0; i < nt; i++) {
        printf("%s{\"y\": %d, \"c\": [%d,%d,%d], \"s\": \"%s\", \"pos\": [", i ? "," : "", drawn[i]->y, drawn[i]->c[0], drawn[i]->c[1], drawn[i]->c[2], drawn[i]->s);
        for (int k = 0; k < drawn[i]->n; k++) printf("%s%d", k ? "," : "", drawn[i]->pos[k]);
        printf("]}");
    }
    printf("]}\n");
}

int main(int argc, char **argv)
{
    ob_init(0);
    if (argc > 1 && !strcmp(argv[1], "render")) {
        for (int a = 2; a < argc; a++) { ob_set(atoi(argv[a])); frame(); }
        return 0;
    }
    /* never the same card twice, all in range, all lines fit the slots */
    for (int i = 0; i < 2000; i++) { int last = ob_card; ob_new_card(); assert(ob_card == last); nt = nl = 0; ob_update(0, 1); assert(ob_card != last && ob_card >= 0 && ob_card < ob_ncards); }
    for (int c = 0; c < ob_ncards; c++) {
        ob_set(c); assert(ob_nl >= 1 && ob_nl <= OB_MAXL);
        for (int i = 0; i < ob_nl; i++) {
            htxt *t = T(ob_txt[OB_SLOT(ob_nl, i)]);
            assert(t->n && t->pos[0] >= 8 && t->pos[t->n - 1] + 12 <= 312);   /* inside the screen */
            for (int k = 0; t->s[k]; k++) assert(!ob_extra(t->s[k]));        /* firmware never sees missing glyphs */
        }
    }
    printf("ok\n");
    return 0;
}
