/* sampler+ core part (docs/46): what stays in the firmware for the sampler+ plugin file (plugins/samplerplus, kind
 * "sampler"). SynthSampler vtable slots render / note_on / note_off and the Adsr_render entry (hook.S) go to the
 * plugin if one is loaded, else to the stock functions (a plain sampler, unchanged); synth patches' "op1r" objects
 * reach the plugin through dp_keep_op1r (drumplus/dp_core.c, kept also without the plugin); the synth type browser
 * job is the plugin's (its 'sampler+' row) or the stock one. */
#include "../op1_synth.h"

const op1_sampler_t *op1_sampler;                             /* set by the plugin loader (plugload.c) */

#define STOCK_RENDER   ((void (*)(void *, int16_t *, const int16_t *))0x01A51A7C)
#define STOCK_NOTE_ON  ((void (*)(void *, const int16_t *))0x01A518F4)
#define STOCK_NOTE_OFF ((void (*)(void *))0x01A51964)
#define STOCK_TYPE_JOB ((void (*)(void *))0x0193FFAC)
int sp_adsr_stock(void *adsr, int16_t *env, const int16_t *words, const int16_t *vel);   /* hook.S */

void sp_fw_render(void *self, int16_t *out, const int16_t *k)
{
    const op1_sampler_t *s = op1_sampler;
    if (s) s->render(self, out, k); else STOCK_RENDER(self, out, k);
}
void sp_fw_note_on(void *self, const int16_t *k) { if (op1_sampler) op1_sampler->note_on(self, k); else STOCK_NOTE_ON(self, k); }
void sp_fw_note_off(void *self) { if (op1_sampler) op1_sampler->note_off(self); else STOCK_NOTE_OFF(self); }
int sp_adsr(void *adsr, int16_t *env, const int16_t *words, const int16_t *vel)
{
    const op1_sampler_t *s = op1_sampler;
    return s ? s->adsr(adsr, env, words, vel) : sp_adsr_stock(adsr, env, words, vel);
}
void sp_slot_loaded(int slot, void *op1r) { if (op1_sampler) op1_sampler->slot_loaded(slot, op1r); }
/* the boot loads the synth patches before the plugins: once the plugin is in, it gets every slot's "op1r" */
#define SYNTH_PATCHES 0x01DDD7E8
#define JV_CGET ((const void *(*)(const void *, const char *))0x019BEB70)
#define JV_KEY  ((void *(*)(void *, const char *))0x019BFEB4)
void sp_replay(void)
{
    for (int slot = 0; slot < 8; slot++) {
        void *json = (uint8_t *)(SYNTH_PATCHES + slot * 0x38) + 4;
        const void *c = JV_CGET(json, "op1r");                   /* const lookup: no member added */
        sp_slot_loaded(slot, c && ((const uint8_t *)c)[8] == 7 ? JV_KEY(json, "op1r") : 0);
    }
}
void sp_type_job(void *job) { if (op1_sampler) op1_sampler->type_job(job); else STOCK_TYPE_JOB(job); }
