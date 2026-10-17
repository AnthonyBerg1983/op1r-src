/* Tool plugins (op1_plugin.h op1_tool_t): key and service dispatch, and the overlay. While a tool's overlay is on,
 * the current page's screen object points at a copy of its vtable whose update runs the page's update, then the
 * tool's draw; the main loop follows screen changes and restores the page once draw returns 0. Two copies used in
 * turn, the original looked up from the object with interrupts off: a call through the last copy stays valid. */
#ifndef OP1_HOST
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#endif
#include "../op1_synth.h"

#ifdef OP1_HOST
extern void **host_screen_table;
extern int host_cur_screen;
#define SCREEN_TABLE  host_screen_table
#define CUR_SCREEN    host_cur_screen
static uint32_t irq_off(void) { return 0; }
static void irq_on(uint32_t m) { (void)m; }
#else
#define SCREEN_TABLE  ((void **)0x01DF4E54)                     /* 0x44 screen objects; vtable at word 3 */
#define CUR_SCREEN    (*(volatile int *)0x018B65C8)
static uint32_t irq_off(void) { uint32_t m; __asm__ volatile ("cli %0;" : "=d"(m)); return m; }
static void irq_on(uint32_t m) { __asm__ volatile ("sti %0;" : : "d"(m)); }
#endif
#define OV_VT_WORDS   10                                        /* screen vtable: 5 {adj, fn} entries */

extern const op1_tool_t *op1_tools[OP1_TOOL_MAX];               /* plugload/plugload.c */
extern int op1_tool_count;

int tool_key(int code, int pressed)                             /* KeyHandler thread: 1 = swallowed */
{
    for (int i = 0; i < op1_tool_count; i++) if (op1_tools[i]->key && op1_tools[i]->key(code, pressed)) return 1;
    return 0;
}

static uintptr_t ov_vt[2][OV_VT_WORDS];                         /* used in turn */
static const uintptr_t *ov_orig[2];                             /* the wrapped objects' own vtables */
static uintptr_t *volatile ov_obj;                              /* the wrapped page object, 0: none */
static const op1_tool_t *volatile ov_tool;
static volatile uint8_t ov_done, ov_gen;
static uint8_t ov_i;
static void ov_update(void *self, int dt)                       /* GUI thread */
{
    uint32_t m = irq_off();
    const uintptr_t *vt = (const uintptr_t *)((uintptr_t *)self)[3];   /* its own once unwrapped */
    if (vt == ov_vt[0]) vt = ov_orig[0]; else if (vt == ov_vt[1]) vt = ov_orig[1];
    const op1_tool_t *t = self == ov_obj && !ov_done ? ov_tool : 0;
    uint8_t g = ov_gen;
    irq_on(m);
    ((void (*)(void *, int))vt[3 * 2 + 1])((uint8_t *)self + (int16_t)vt[3 * 2], dt);   /* entries {adj, fn} */
    if (t && !t->draw(op1_raster())) {
        m = irq_off();
        if (g == ov_gen) ov_done = 1;                           /* not after a newer op1_overlay */
        irq_on(m);
    }
}
static void ov_unwrap(void) { if (ov_obj) { ov_obj[3] = (uintptr_t)ov_orig[ov_i]; ov_obj = 0; } }   /* interrupts off */
static void ov_wrap(uintptr_t *obj)                             /* interrupts off */
{
    if (obj == ov_obj) return;
    ov_unwrap();
    ov_i ^= 1;
    ov_orig[ov_i] = (const uintptr_t *)obj[3];
    for (int i = 0; i < OV_VT_WORDS; i++) ov_vt[ov_i][i] = ov_orig[ov_i][i];
    ov_vt[ov_i][3 * 2] = 0; ov_vt[ov_i][3 * 2 + 1] = (uintptr_t)ov_update;
    obj[3] = (uintptr_t)ov_vt[ov_i]; ov_obj = obj;
}
static uintptr_t *page_obj(void)                                /* the current screen's object, 0: none */
{
    int id = CUR_SCREEN;
    return id >= 0 && id < 0x44 ? (uintptr_t *)SCREEN_TABLE[id] : 0;
}
void op1_overlay(const op1_tool_t *t)
{
    uintptr_t *obj = page_obj();
    if (!t || !t->draw || !obj) return;
    uint32_t m = irq_off();
    ov_wrap(obj);
    ov_tool = t; ov_done = 0; ov_gen++;
    irq_on(m);
}
void tool_service(void)                                         /* main loop: services, then the overlay */
{
    for (int i = 0; i < op1_tool_count; i++) if (op1_tools[i]->service) op1_tools[i]->service();
    if (!ov_obj) return;
    uintptr_t *obj = page_obj();
    uint32_t m = irq_off();
    if (ov_done || !obj) { ov_unwrap(); ov_tool = 0; }
    else if (obj != ov_obj) ov_wrap(obj);
    irq_on(m);
}
