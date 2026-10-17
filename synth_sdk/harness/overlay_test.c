/* Host test of screens/overlay.c: tool key / service dispatch and the overlay over fake screen objects.
 *   mkdir -p build && cc -DOP1_HOST -I.. -Wall -Wextra -fsanitize=address,undefined overlay_test.c ../screens/overlay.c \
 *     -o build/overlay_test && build/overlay_test */
#include "../op1_synth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int tool_key(int code, int pressed);
void tool_service(void);
const op1_tool_t *op1_tools[OP1_TOOL_MAX];
int op1_tool_count;
static void *table[0x44];
void **host_screen_table = table;
int host_cur_screen;
void *host_raster(void) { return table; }

static char trace[64]; static int nt;
static void mark(char c) { if (nt < 63) { trace[nt++] = c; trace[nt] = 0; } }
static void upd_a(void *self, int dt) { (void)self; (void)dt; mark('A'); }
static void upd_b(void *self, int dt) { (void)self; (void)dt; mark('B'); }
static void upd_c(void *self, int dt) { (void)self; (void)dt; mark('C'); }
static void other(void *self, int dt) { (void)self; (void)dt; mark('x'); }
static uintptr_t vt_a[10], vt_b[10], vt_c[10];
static uintptr_t obj_a[8], obj_b[8], obj_c[8];            /* screen objects: vtable at word 3 */
static int draw_ret = 1, renew;
static const op1_tool_t tool;
static int t_draw(void *r)
{
    assert(r == table); mark('D');
    if (renew) { renew = 0; op1_overlay(&tool); }        /* a press during the draw */
    return draw_ret;
}
static const op1_tool_t tool = { 0, 0, t_draw };
static const char *frame(uintptr_t *o)                    /* the GUI's call of a page's update (entry 3) */
{
    const uintptr_t *vt = (const uintptr_t *)o[3];
    nt = 0; trace[0] = 0;
    ((void (*)(void *, int))vt[7])((uint8_t *)o + (int16_t)vt[6], 16);
    return trace;
}

static int keys[2], served[2];
static int key0(int code, int pressed) { (void)pressed; keys[0]++; return code == 5; }
static int key1(int code, int pressed) { (void)pressed; keys[1]++; return code == 5 || code == 6; }
static void serve0(void) { served[0]++; }
static void serve1(void) { served[1]++; }
static const op1_tool_t t0 = { key0, serve0, 0 }, t1 = { key1, serve1, 0 };

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    for (int i = 0; i < 10; i++) vt_a[i] = vt_b[i] = vt_c[i] = (uintptr_t)other;
    vt_a[6] = vt_b[6] = vt_c[6] = 0;
    vt_a[7] = (uintptr_t)upd_a; vt_b[7] = (uintptr_t)upd_b; vt_c[7] = (uintptr_t)upd_c;
    obj_a[3] = (uintptr_t)vt_a; obj_b[3] = (uintptr_t)vt_b; obj_c[3] = (uintptr_t)vt_c;
    table[0x10] = obj_a; table[0x16] = obj_b; host_cur_screen = 0x10;
    /* no tools: keys pass, nothing touched */
    assert(!tool_key(0x0B, 1));
    tool_service();
    assert(obj_a[3] == (uintptr_t)vt_a && !strcmp(frame(obj_a), "A"));
    /* overlay: the page's update, then the draw; the other entries copied */
    op1_overlay(&tool);
    assert(obj_a[3] != (uintptr_t)vt_a && ((const uintptr_t *)obj_a[3])[5] == vt_a[5]);
    assert(!strcmp(frame(obj_a), "AD"));
    op1_overlay(&tool);                                    /* a second call: no double wrap */
    assert(!strcmp(frame(obj_a), "AD"));
    /* another screen: the old object restored, the new one wrapped */
    host_cur_screen = 0x16; tool_service();
    assert(obj_a[3] == (uintptr_t)vt_a && !strcmp(frame(obj_a), "A"));
    assert(!strcmp(frame(obj_b), "BD"));
    /* object swap at the same id */
    table[0x16] = obj_c; tool_service();
    assert(obj_b[3] == (uintptr_t)vt_b && !strcmp(frame(obj_c), "CD"));
    table[0x16] = obj_b; tool_service();
    assert(obj_c[3] == (uintptr_t)vt_c && !strcmp(frame(obj_b), "BD"));
    /* draw returns 0: no more draws, the page restored by the main loop */
    draw_ret = 0;
    assert(!strcmp(frame(obj_b), "BD"));
    assert(!strcmp(frame(obj_b), "B"));
    tool_service();
    assert(obj_b[3] == (uintptr_t)vt_b && !strcmp(frame(obj_b), "B"));
    /* newer overlay during a draw that returns 0: it stays */
    draw_ret = 1; op1_overlay(&tool);
    draw_ret = 0; renew = 1;
    assert(!strcmp(frame(obj_b), "BD"));
    draw_ret = 1; tool_service();
    assert(obj_b[3] != (uintptr_t)vt_b && !strcmp(frame(obj_b), "BD"));
    draw_ret = 0; frame(obj_b); tool_service();
    assert(obj_b[3] == (uintptr_t)vt_b);
    /* no screen object: nothing */
    host_cur_screen = 0x30; op1_overlay(&tool);
    host_cur_screen = 0x44; op1_overlay(&tool); tool_service();
    assert(obj_a[3] == (uintptr_t)vt_a && obj_b[3] == (uintptr_t)vt_b && obj_c[3] == (uintptr_t)vt_c);
    /* a tool without draw: no overlay */
    static const op1_tool_t nodraw = { 0, 0, 0 };
    host_cur_screen = 0x10; op1_overlay(&nodraw);
    assert(obj_a[3] == (uintptr_t)vt_a);
    printf("overlay ok\n");
    /* tools: keys and services in load order; the first that swallows wins */
    op1_tools[0] = &t0; op1_tools[1] = &t1; op1_tool_count = 2;
    assert(tool_key(5, 1) && keys[0] == 1 && keys[1] == 0);
    assert(tool_key(6, 1) && keys[0] == 2 && keys[1] == 1);
    assert(!tool_key(7, 0) && keys[0] == 3 && keys[1] == 2);
    tool_service();
    assert(served[0] == 1 && served[1] == 1);
    printf("tools ok\n");
    return 0;
}
