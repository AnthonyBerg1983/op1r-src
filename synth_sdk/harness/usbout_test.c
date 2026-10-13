/* Host test + preview frames for screens/usbout.c (USB out page): toggling, live flag, saved value, "restart" hint.
 * "render": JSON frames for harness/render_usbout.py */
#include "../op1_synth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../screens/usbout.c"

volatile uint8_t usb_tracks_live;
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
    nl = nt = 0; us_update(0, 1);
    if (!render) return;
    printf("{\"label\": \"%s\", \"lines\": [", label);
    for (int i = 0; i < nl; i++) printf("%s[%d,%d,%d,%d,%d,[%d,%d,%d,%d]]", i ? "," : "", L[i].x0, L[i].y0, L[i].x1, L[i].y1, L[i].w, L[i].c[0], L[i].c[1], L[i].c[2], L[i].c[3]);
    printf("], \"text\": [");
    for (int i = 0; i < nt; i++) printf("%s[%d,%d,[%d,%d,%d],\"%s\"]", i ? "," : "", drawn[i]->x, drawn[i]->y, drawn[i]->c[0], drawn[i]->c[1], drawn[i]->c[2], drawn[i]->s);
    printf("]}\n");
}

int main(int argc, char **argv)
{
    int render = argc > 1 && !strcmp(argv[1], "render");
    usbout_host_saved = 0; usb_tracks_live = (uint8_t)usbout_load();
    us_init(0);
    frame("master (default)", render);
    usbout_turn(1024);
    assert(usb_tracks_live == 1 && usbout_host_saved == 1);
    frame("blue right: master + tracks", render);
    usbout_turn(-1024);
    assert(usb_tracks_live == 0 && usbout_host_saved == 0);

    usbout_turn(-1024); assert(usbout_host_saved == 0);      /* already there: no change */
    usbout_turn(300); usbout_turn(300); assert(usb_tracks_live == 0);   /* < 1 detent */
    if (!render) printf("ok\n");
    return 0;
}
