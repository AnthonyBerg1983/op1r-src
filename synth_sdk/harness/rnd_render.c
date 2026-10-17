/* Preview frames of the synth randomiser panel (rnd/rn_draw.c) for render_rnd.py, one JSON frame per line; asserts
 * that every line stays on screen and that the right centred text is drawn. */
#include "../op1_synth.h"
#include "../rnd/rn_draw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAXL 64
static struct { int32_t x0, y0, x1, y1, w; uint8_t c[4]; } lines[MAXL]; static int nlines;
void *host_raster(void) { return &nlines; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c)
{
    (void)r; const op1_pt *p = a, *q = b;
    assert(nlines < MAXL);
    lines[nlines].x0 = p->x; lines[nlines].y0 = p->y; lines[nlines].x1 = q->x; lines[nlines].y1 = q->y; lines[nlines].w = w;
    memcpy(lines[nlines].c, c, 4); nlines++;
}
typedef struct { int32_t x, y, sz; uint8_t c[4]; int align; char s[32]; } txt_t;
static txt_t *drawn[4]; static int nt;
static int dims, dim_at;
void host_raster_dim(void *r) { (void)r; dims++; dim_at = nlines; }
void *host_text_ctor(void *o, int32_t x, int32_t y, int32_t sz, const uint8_t *c)
{ txt_t *t = o; t->x = x; t->y = y; t->sz = sz; memcpy(t->c, c, 4); t->s[0] = 0; t->align = 0; return o; }
void host_text_align(void *o, int a) { ((txt_t *)o)->align = a; }
void host_text_set(void *o, const char *s) { snprintf(((txt_t *)o)->s, 32, "%s", s); }
void host_text_draw(void *o, void *r, const int32_t *m) { (void)r; (void)m; if (nt < 4) drawn[nt++] = o; }
static int on_screen(int32_t x, int32_t y) { return x >= 0 && x <= OP1_Q10(320) && y >= 0 && y <= OP1_Q10(160); }
static void frame(const char *label, int back)
{
    nlines = 0; nt = 0; dims = 0;
    rn_draw_panel(&nlines, back);
    assert(nt == 1 && !strcmp(drawn[0]->s, back ? "modnar" : "random") && drawn[0]->align == 1);
    assert(dims == 1 && dim_at == 0);                      /* the page dimmed first */
    printf("{\"label\": \"%s\", \"dim\": %d, \"lines\": [", label, dims);
    for (int i = 0; i < nlines; i++) {
        assert(on_screen(lines[i].x0, lines[i].y0) && on_screen(lines[i].x1, lines[i].y1));
        printf("%s[%d, %d, %d, %d, %d, [%d, %d, %d, %d]]", i ? ", " : "", lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1,
               lines[i].w, lines[i].c[0], lines[i].c[1], lines[i].c[2], lines[i].c[3]);
    }
    printf("], \"text\": [[%d, %d, [%d, %d, %d], \"%s\", %d, %d]]}\n", drawn[0]->x, drawn[0]->y, drawn[0]->c[0], drawn[0]->c[1],
           drawn[0]->c[2], drawn[0]->s, drawn[0]->align, drawn[0]->sz);
}
void rn_fade(void *r);
static void test_fade(void)                              /* every pixel at half, rows by stride, padding untouched */
{
    static uint16_t px[2][4] = { { 0xFFFF, 0xF800, 0x07E0, 0xBEEF }, { 0x001F, 0x0000, 0x8410, 0xBEEF } };
    struct { uint16_t *px; uint32_t w, stride, h; } r = { &px[0][0], 3, 8, 2 };
    rn_fade(&r);
    assert(px[0][0] == 0x7BEF && px[0][1] == 0x7800 && px[0][2] == 0x03E0 && px[1][0] == 0x000F);
    assert(px[1][1] == 0 && px[1][2] == 0x4208 && px[0][3] == 0xBEEF && px[1][3] == 0xBEEF);
}
int main(void) { test_fade(); frame("Shift + drop", 0); frame("Shift + lift", 1); return 0; }
