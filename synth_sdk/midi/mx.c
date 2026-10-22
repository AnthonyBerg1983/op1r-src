/* MIDI automation of the current synth slot (docs/41): absolute CCs on the OP-1 channel set knobs like turning
 * them, stored in the patch like the stock knobs.
 *   CC 53-56  engine knobs 1-4      CC 57-60  envelope A D S R
 *   CC 72-75  synth FX knobs 1-4    CC 76-79  synth LFO knobs 1-4
 * The CC (timer context: midi_cc_hook, or the sequencer's audio thread) only stores the value and queues key code
 * MX_KEY into the KeyHandler queue; the KeyHandler thread (key_event_hook -> mx_apply) writes the live word and calls
 * the patch setter, as the synth controller's on_knob 0x01A505BC does for each page:
 *   engine   words 0xFF8013C4 + slot*16, range 0x01DE5890 + engine*0x40 + k*8 {min, max, scale, mode}, set_knobs 0x01A00D14
 *   envelope words 0x01B60EF0 + slot*16, range 0x01DE5710 + k*6 {min, max, scale},             set 0x01A00C98
 *   FX       words 0xFF801444 + slot*16, range 0x01DE5C50 + fx type*0x30 + k*6,                set_fx 0x019FF664
 *   LFO      words 0xFF8014C4 + slot*16, range 0x01DE5740 + lfo type*0x30 + k*6,               set_lfo 0x019FF734
 * Skipped (special stock handling, not a plain range): engine knobs with range mode 2/3 (sample start / end
 * pairs), LFO types 4 (MIDI / quad: packed words), 5 knob 4, 6 knob 1; FX / LFO when the slot has it switched off. */
#include "../op1_synth.h"
#include "mx.h"

int mx_index(int cc)                                      /* CC -> 0..15 (group * 4 + knob), -1: not ours */
{
    if (cc >= 53 && cc <= 60) return cc - 53;
    if (cc >= 72 && cc <= 79) return cc - 64;
    return -1;
}
int mx_scale(const int16_t *rg, int v)                    /* 0..127 over [min, max] */
{
    int lo = rg[0], hi = rg[1];
    if (hi <= lo) return lo;
    return lo + (hi - lo) * (v & 127) / 127;
}

#ifndef OP1_HOST
static volatile uint8_t val[16];
static volatile uint16_t pend;
static volatile uint8_t queued;
static uint32_t irq_off(void) { uint32_t m; __asm__ volatile ("cli %0;" : "=d"(m)); return m; }
static void irq_on(uint32_t m) { __asm__ volatile ("sti %0;" : : "d"(m)); }

int mx_cc(int cc, int v)
{
    int i = mx_index(cc);
    if (i < 0) return 0;
    uint32_t m = irq_off();
    val[i] = (uint8_t)(v & 127); pend |= (uint16_t)(1u << i);
    int push = !queued;
    if (push) {                                           /* one pending MX_KEY event at a time (as kb_push in ms_fw.c) */
        volatile uint32_t *rd = (volatile uint32_t *)0x01DD5244, *wr = (volatile uint32_t *)0x01DD5248;
        uint32_t w = *wr, n = (w + 1) & 0xFF;
        if (n != *rd && !*(volatile uint8_t *)0x01DD524D) { ((volatile uint16_t *)0x01DD5044)[w] = (uint16_t)(MX_KEY | MX_KEY_DOWN << 8); *wr = n; queued = 1; }
        else push = 0;
    }
    irq_on(m);
    if (push) ((void (*)(void))0x019C2F04)();             /* wake KeyHandler */
    return 1;
}

#define SLOT     (*(volatile int *)0xFF8016D4 & 7)
#define PATCH(s) ((void *)(0x01DDD7E8 + (s) * 0x38))
typedef void (*setter)(void *, int16_t *);
static void apply(int i, int v)
{
    int s = SLOT, k = i & 3;
    int16_t *w; const int16_t *rg; setter set;
    switch (i >> 2) {
    case 0: {
        int eng = ((volatile int *)0xFF8016E4)[s];
        if (eng < 0 || eng > 31) return;
        rg = (const int16_t *)(0x01DE5890 + eng * 0x40 + k * 8);
        if (rg[3] != 0 && rg[3] != 1) return;             /* linked sample ranges: stock-only */
        w = (int16_t *)(0xFF8013C4 + s * 16); set = (setter)0x01A00D14;
        break;
    }
    case 1:
        rg = (const int16_t *)(0x01DE5710 + k * 6);
        w = (int16_t *)(0x01B60EF0 + s * 16); set = (setter)0x01A00C98;
        break;
    case 2: {
        if (!((int (*)(int))0x0196727C)(s)) return;       /* FX off in this slot */
        int t = ((volatile int *)0xFF801734)[s];
        if (t < 0 || t > 15) return;
        rg = (const int16_t *)(0x01DE5C50 + t * 0x30 + k * 6);
        w = (int16_t *)(0xFF801444 + s * 16); set = (setter)0x019FF664;
        break;
    }
    default: {
        if (!((int (*)(int))0x0196726C)(s)) return;       /* LFO off */
        int t = ((volatile int *)0xFF801384)[s];
        if (t < 0 || t > 15 || t == 4 || (t == 5 && k == 3) || (t == 6 && k == 0)) return;
        rg = (const int16_t *)(0x01DE5740 + t * 0x30 + k * 6);
        w = (int16_t *)(0xFF8014C4 + s * 16); set = (setter)0x019FF734;
        break;
    }
    }
    if (rg[1] <= rg[0]) return;                           /* range table not filled */
    w[k] = (int16_t)mx_scale(rg, v);
    set(PATCH(s), w);
}
void mx_apply(void)                                       /* KeyHandler thread: MX_KEY from the queue */
{
    uint32_t m = irq_off();
    uint16_t p = pend; pend = 0; queued = 0;
    uint8_t v[16];
    for (int i = 0; i < 16; i++) v[i] = val[i];
    irq_on(m);
    for (int i = 0; i < 16; i++) if (p >> i & 1) apply(i, v[i]);
}
#endif
