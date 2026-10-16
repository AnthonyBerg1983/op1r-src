/* sampler+: model of the stock synth sampler voice (SynthSampler render 0x01A51A7C, note_on 0x01A518F4, note_off
 * 0x01A51964; docs/46), bit-exact against the firmware (harness/sp_voice_test.c, emulator dump). Integer only. */
#ifndef SP_VOICE_H
#define SP_VOICE_H
#include <stdint.h>

#define SP_BLOCK 128
typedef struct {                       /* the stock voice object's fields (+8 pos, +0xC released, +0xD first block) */
    int32_t pos;                       /* 1/4096 sample of the buffer (incl. its 8 guard samples) */
    uint8_t released, first;
} sp_voice;

/* knobs: the slot's 8 words (0 start, 1 / 5 loop start + fine, 2 / 6 loop end + fine, 3 end, 4 >= 0x4000 reverse,
 * 7 gain); inc: the phase-increment bus (0xFF801950); rate: sampler object +4; data: buffer incl. guard;
 * fir: interpolation table centre (0xFF8021D2: tap k of phase p at fir[k * 64 - p]) */
void sp_stock_note_on(sp_voice *v, const int16_t *knobs);
void sp_stock_note_off(sp_voice *v);
void sp_stock_render(sp_voice *v, int16_t *out, const int16_t *knobs, const int32_t *inc, int32_t rate,
                     const int16_t *data, const int16_t *fir);
int32_t sp_bound(int16_t hi_w, int16_t lo_w);          /* buffer fraction words -> pos */
int32_t sp_step(int32_t inc, int32_t rate);            /* per-sample pos step */
int16_t sp_tap(const int16_t *data, const int16_t *fir, int32_t pos, int32_t g);
#endif
