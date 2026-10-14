/* GUI frame rate (docs/33): the stock Screen::frame 0x01A3BB20 renders every visible screen every GUI tick
 * (60 fps), also when nothing changes (AGG rasteriser: 10 % CPU on the idle tape screen, ~48 % on the sampler
 * screen, docs/32). Its raster-clear CALL (0x01A3BBE0) jumps to gui_frame_stub (hook.S) -> gui_frame: render at the
 * capped rate while something moves or happens, else every GUI_IDLE_DIV-th tick. Skipped ticks keep the last image
 * and their time goes to the next update (screens animate by dt). PlayerManager / MainInterface after the loop
 * still run every tick (the stub returns to 0x01A3BC02).
 * Double buffering is ours: the driver hands out 0x03000000 / 0x02000000 alternately ([0x01D08520]), but a render
 * always goes to the buffer that is *not* on display (raster re-targeted, cache flushed, submitted by gui_submit,
 * the CALL 0x01A2FB18 at 0x019AB8CE); a skipped tick submits the shown buffer again. So any period works.
 * "Something moves": input (5 s), sound / tape (1 s), or the rendered frame differs from the previous one (sparse
 * checksum vs the same buffer's previous frame, 1 s) - this keeps animated screens (LFOs, resource monitor, sequencers) at the capped rate. */
#include "../../op1_synth.h"

#define GUI_IDLE_DIV   5                    /* idle: 60 / 5 = 12 fps (to see the next change) */
#define GUI_HOLD_TICKS 60                   /* capped rate for 1 s after sound / tape */
#define GUI_HOLD_ANIM  30                   /* 0.5 s once 3 rendered frames in a row changed (animation): a frame whose
                                               motion the sparse checksum misses does not drop the rate. (A single change
                                               shows twice: in both buffers, each compared with its own last frame.) */
/* after a single changed frame: 2 cap periods (an animation changes every rendered frame and stays at the cap; a screen
 * that changes a few times per second, as the resource monitor, idles in between) */
#define GUI_HOLD_INPUT 300                  /* and 5 s after input: some UI fades step per update, not by dt */
#define GUI_PEAK_MIN   64                   /* synth bus peak (of 32767) that counts as sound */

#ifdef OP1_HOST
extern int host_gui_input, host_gui_tape, host_gui_peak;
#define TAPE_MODE   host_gui_tape
#define BUS_PEAK    host_gui_peak
#else
#define TAPE_MODE   (*(volatile int32_t *)0xFF802960)            /* 0 stopped, 1 rec, 2 play, 3 winding */
#define BUS_PEAK    (*(volatile int16_t *)0xFF80108E)            /* synth bus peak (instrument + FX) */
#endif

volatile uint8_t gui_input;                 /* set by the key / encoder hooks (dispatcher.c) */
/* frame-rate cap (USB out page, green): render every 1st / 2nd / 3rd / 5th tick at most = 60 / 30 / 20 / 12 fps */
static const uint8_t gui_cap_div[4] = { 1, 2, 3, 5 };
volatile uint8_t gui_cap;                   /* 0..3 */
uint32_t gui_stat[2];                       /* rendered / skipped ticks (resource monitor, tests) */
static int32_t hold, tick, acc, run_changed;

/* 1 = render this tick; changed: the last rendered frame differed from the one before */
int gui_should_render(int dt, int changed)
{
    acc += dt;
    int active = TAPE_MODE != 0 || BUS_PEAK > GUI_PEAK_MIN || BUS_PEAK < -GUI_PEAK_MIN;
    if (gui_input) hold = GUI_HOLD_INPUT;
    else if (active && hold < GUI_HOLD_TICKS) hold = GUI_HOLD_TICKS;
    else if (changed) {
        int h = ++run_changed >= 3 ? GUI_HOLD_ANIM : 2 * gui_cap_div[gui_cap < 4 ? gui_cap : 0];
        if (hold < h) hold = h;
    }
    else if (hold) hold--;
    if (!changed && tick == 0) run_changed = 0;               /* (changed is per rendered frame) */
    gui_input = 0;
    int cap = gui_cap_div[gui_cap < 4 ? gui_cap : 0], period = hold ? cap : (cap > GUI_IDLE_DIV ? cap : GUI_IDLE_DIV);
    if (tick < period) tick++;
    if (tick >= period) { tick = 0; gui_stat[0]++; return 1; }
    gui_stat[1]++;
    return 0;
}
int gui_take_dt(void) { int d = acc; acc = 0; return d; }   /* time since the last rendered tick */

/* sparse checksum of a rendered frame: every 2nd pixel pair of every 2nd row (400 x 198, stride 1024 bytes) */
uint32_t gui_frame_sum(const uint8_t *buf)
{
    uint32_t h = 0;
    for (int y = 0; y < 198; y += 2) {
        const uint32_t *row = (const uint32_t *)(buf + y * 1024);
        for (int x = 0; x < 200; x += 2) h = (h << 5 | h >> 27) ^ row[x];
    }
    return h;
}

#ifndef OP1_HOST
#define RASTER        (*(void *volatile *)0x01DF4E34)
#define RASTER_CLEAR  ((void (*)(void *))0x01A23466)
#define RASTER_TARGET ((void (*)(void *, uint32_t))0x01A23442)   /* as gui_tick 0x019B2674 does it */
#define GUI_RASTER    (*(void *volatile *)0x01DCE8CC)
#define CUR_SCREEN    (*(volatile int *)0x018B65C8)
#define RENDER_TARGET (*(volatile uint32_t *)0x01D08520)
#define FB_ROWS       (*(volatile int32_t *)0xFF8017B4)          /* flush size = rows << 9 (as the GUI loop) */
#define CACHE_FLUSH   ((void (*)(uint32_t, uint32_t, int))0x01AA185C)
#define DISPLAY_SUBMIT ((void (*)(uint32_t))0x01A2FB18)
static uint32_t shown, other, back, last_sum[2], last_buf[2];   /* checksum per buffer (they differ outside the drawing) */
static uint8_t rendered, changed;

/* the clear + update loop of Screen::frame (0x01A3BBE0..0x01A3BC00), or nothing */
void gui_frame(uint8_t **table, int dt)
{
    uint32_t t = RENDER_TARGET;
    if (t != shown) other = t;                                   /* the two buffers: shown and other */
    rendered = (uint8_t)gui_should_render(dt, changed);
    changed = 0;
    if (!rendered) return;
    back = shown && t == shown ? other : t;                      /* never the buffer on display */
    if (!back) back = t;
    if (back != t) RASTER_TARGET(GUI_RASTER, back);
    int d = gui_take_dt(), cur = CUR_SCREEN;
    RASTER_CLEAR(RASTER);
    for (int i = 0; i < 0x44; i++) {
        uint8_t *scr = table[i];
        const uint8_t *vt = *(const uint8_t *const *)(scr + 0xC);
        int (*visible)(void *, int) = *(int (*const *)(void *, int))(vt + 0x14);
        if (!visible(scr + *(const int16_t *)(vt + 0x10), cur)) continue;
        void (*update)(void *, int) = *(void (*const *)(void *, int))(vt + 0x1C);
        update(scr + *(const int16_t *)(vt + 0x18), d);
    }
    uint32_t s = gui_frame_sum((const uint8_t *)back);
    int k = last_buf[0] == back ? 0 : last_buf[1] == back ? 1 : (last_buf[0] ? 1 : 0);
    changed = last_buf[k] != back || s != last_sum[k];         /* vs this buffer's previous frame */
    last_buf[k] = back; last_sum[k] = s;
}

/* CALL 0x01A2FB18(buf) in the GUI loop (after its flush of buf): show our back buffer, or the shown one again */
void gui_submit(uint32_t buf)
{
    if (rendered) {
        if (back != buf) CACHE_FLUSH(back, back + ((uint32_t)FB_ROWS << 9), 0);
        shown = back;
    } else if (!shown) shown = buf;
    DISPLAY_SUBMIT(shown);
}
#endif
