/* sampler+ plugin (plugins/samplerplus, kind "sampler", docs/46): the core (sp_core.c) forwards the SynthSampler
 * vtable slots and Adsr_render here. A slot is sampler+ when its patch has "op1r": {"engine": "sampler+"} and its type
 * is the sampler (6); every other slot / voice calls the stock functions, unchanged. */
#include "../op1_synth.h"
#include "sp_plus.h"

#define SP_SLOTS   8
#define SP_VOICES  6
#define VOICE0     0xFFB00000u                                /* SynthSampler voice objects, 0x10 each */
#define ADSR0      0xFF8000D4u                                /* ADSR objects, 0x18 each (Adsr::get_voice) */
#define VOICE_VEL  ((const volatile int16_t *)0xFF801064)     /* velocity << 8 of voice v */
#define SLOT_TYPE  ((const volatile int32_t *)0xFF8016E4)     /* synth slot types (6 = sampler) */
#define FIR        ((const int16_t *)0xFF8021D2)
#define BUF_INC    ((int32_t *)0xFF801950)                    /* refilled for every voice before its render */
#define BEAT_LEN   (*(const volatile int32_t *)0x01AADAD8)    /* metronome beat length, 0x4000 per sample */
#define SYNTH_PATCHES 0x01DDD7E8                              /* 8 synth patch objects x 0x38 (JSON at +4) */
#define STOCK_RENDER   ((void (*)(void *, int16_t *, const int16_t *))0x01A51A7C)
#define STOCK_NOTE_ON  ((void (*)(void *, const int16_t *))0x01A518F4)
#define STOCK_NOTE_OFF ((void (*)(void *))0x01A51964)
#define STOCK_TYPE_JOB ((void (*)(void *))0x0193FFAC)
int sp_adsr_stock(void *adsr, int16_t *env, const int16_t *words, const int16_t *vel);   /* core export */

sp_extra sp_x[SP_SLOTS];                                     /* shared with the pages (sp_ui.c) */
sp_pre   sp_c[SP_SLOTS];
uint8_t  sp_isplus[SP_SLOTS], sp_keep_next[SP_SLOTS];          /* keep: the browser's preset load stays sampler+ */
static sp_xvoice sp_xs[SP_VOICES];
static uint8_t  sp_vpend[SP_VOICES];                         /* velocity not applied yet (first render) */
static int32_t  sp_sync_key[SP_SLOTS][3];                    /* bar length, start, end the sync factor was made for */

static int cur_slot(void)
{
    int s = op1_current_slot();
    return (unsigned)s < SP_SLOTS && sp_isplus[s] && SLOT_TYPE[s] == 6 ? s : -1;
}
static int voice_of(void *self) { uint32_t d = (uint32_t)self - VOICE0; return d < SP_VOICES * 0x10 && !(d & 15) ? (int)(d >> 4) : -1; }
static sp_voice *model(void *self) { return (sp_voice *)((uint8_t *)self + 8); }   /* pos, released, first */

static void spx_render(void *self, int16_t *out, const int16_t *k)
{
    int s = cur_slot(), v = voice_of(self);
    if (s < 0 || v < 0) { STOCK_RENDER(self, out, k); return; }
    const uint32_t *obj = *(const uint32_t *const *)((uint8_t *)self + 4);   /* sampler object: SampleBuffer, rate */
    const int16_t *data = *(const int16_t *const *)obj[0];
    if (sp_vpend[v]) { sp_vpend[v] = 0; sp_plus_velocity(model(self), &sp_xs[v], k, &sp_x[s], VOICE_VEL[v] >> 8); }
    if (sp_x[s].sync) {
        int32_t bar = (BEAT_LEN >> 14) * 4;
        if (bar != sp_sync_key[s][0] || k[0] != sp_sync_key[s][1] || k[3] != sp_sync_key[s][2]) {   /* rare: division */
            sp_sync_key[s][0] = bar; sp_sync_key[s][1] = k[0]; sp_sync_key[s][2] = k[3];
            sp_plus_sync(&sp_x[s], &sp_c[s], k, (int32_t)obj[1], bar > 0 ? (uint32_t)bar : 88200);
        }
    }
    if (sp_plus_fast_ok(&sp_x[s])) {                              /* the stock (assembler) render + pre / post */
        sp_plus_fast_pre(&sp_xs[v], &sp_c[s], BUF_INC);
        STOCK_RENDER(self, out, k);
        sp_plus_fast_post(&sp_xs[v], &sp_x[s], &sp_c[s], out);
    } else
        sp_plus_render(model(self), &sp_xs[v], out, k, &sp_x[s], &sp_c[s], BUF_INC, (int32_t)obj[1], data, FIR);
}
static void spx_note_on(void *self, const int16_t *k)
{
    int s = cur_slot(), v = voice_of(self);
    if (s < 0 || v < 0) { STOCK_NOTE_ON(self, k); return; }
    sp_plus_note_on(model(self), &sp_xs[v], k, &sp_x[s], &sp_c[s]);
    sp_vpend[v] = 1;
}
static void spx_note_off(void *self)                          /* released flag (stops the loop); the envelope */
{                                                              /* follows the ADSR state */
    int s = cur_slot();
    if (s >= 0 && sp_x[s].loop_rel && voice_of(self) >= 0) return;   /* loop in release: keep looping */
    STOCK_NOTE_OFF(self);
}
static int spx_adsr(void *adsr, int16_t *env, const int16_t *words, const int16_t *vel)
{
    uint32_t d = (uint32_t)adsr - ADSR0;
    int s = cur_slot(), v = d < SP_VOICES * 0x18 ? (int)(d / 0x18) : -1;
    if (s < 0 || v < 0) return sp_adsr_stock(adsr, env, words, vel);
    int32_t *o = (int32_t *)adsr;
    int32_t level = sp_plus_level(&sp_x[s], *vel, (int16_t)o[5]);
    int done = sp_plus_env(&sp_xs[v], &sp_x[s], &sp_c[s], &o[0], env, level, o[1] == 0);   /* counter 0: a new note */
    o[1]++;                                                   /* block counter, as the stock */
    return done;
}

/* ---- persistence: "op1r": {"engine": "sampler+", "p": [field 0 .. SP_NFIELDS-1]} in the synth patch JSON ---- */
#define JV_KEY    ((void *(*)(void *, const char *))0x019BFEB4)
#define JV_IDX    ((void *(*)(void *, unsigned))0x019BFAF8)
#define JV_ASSIGN ((void *(*)(void *, const void *))0x019BDDC0)
#define JV_SIZE   ((unsigned (*)(const void *))0x019BE8D8)
#define JV_FROM_INT ((void *(*)(void *, int))0x019BD890)
#define JV_FROM_STR ((void *(*)(void *, const char *))0x019BDC4C)
#define JV_DTOR   ((void (*)(void *, int))0x019BDD18)
#define JV_REMOVE ((void (*)(void *, void *, const char *))0x019BEE88)
enum { JV_NULL = 0, JV_INT = 1, JV_UINT = 2, JV_STRING = 4, JV_ARRAY = 6, JV_OBJECT = 7 };
static uint8_t jv_type(const void *v) { return ((const uint8_t *)v)[8]; }
static int32_t jv_int(const void *v)
{
    int32_t w = *(const int32_t *)v;
    return jv_type(v) == JV_INT || (jv_type(v) == JV_UINT && w >= 0) ? w : 0;
}
static int jv_is_str(void *v, const char *s)
{
    if (jv_type(v) != JV_STRING) return 0;
    const char *p = *(const char *const *)v;
    if (!p) return 0;
    while (*s && *p == *s) p++, s++;
    return *p == *s;
}

static void spx_slot_loaded(int slot, void *op1r)
{
    if ((unsigned)slot >= SP_SLOTS) return;
    int plus = op1r && jv_type(op1r) == JV_OBJECT && jv_is_str(JV_KEY(op1r, "engine"), "sampler+");
    sp_extra_default(&sp_x[slot]);
    if (plus) {
        void *p = JV_KEY(op1r, "p");
        if (jv_type(p) == JV_ARRAY)
            for (unsigned i = 0, n = JV_SIZE(p); i < n && i < SP_NFIELDS; i++) sp_extra_set(&sp_x[slot], (int)i, jv_int(JV_IDX(p, i)));
    }
    sp_plus_prepare(&sp_x[slot], &sp_c[slot]);
    sp_sync_key[slot][0] = -1;
    sp_isplus[slot] = (uint8_t)plus;
}

/* op1r.p = the slot's fields (UI edits); the caller marks the patch changed */
void sp_fw_store(int slot)
{
    if ((unsigned)slot >= SP_SLOTS) return;
    void *op = JV_KEY((uint8_t *)(SYNTH_PATCHES + slot * 0x38) + 4, "op1r");
    if (jv_type(op) != JV_OBJECT) return;
    void *p = JV_KEY(op, "p");
    uint32_t tmp[4];
    for (int i = 0; i < SP_NFIELDS; i++) {
        JV_FROM_INT(tmp, sp_extra_get(&sp_x[slot], i));
        JV_ASSIGN(JV_IDX(p, (unsigned)i), tmp);
        JV_DTOR(tmp, 2);
    }
}

/* ---- synth type browser: the 'sampler+' row (id OP1_SAMPLER_ROW). A sampler slot keeps its sample and settings and
 * gets "op1r": {"engine": "sampler+", "p": defaults}; another type first becomes a plain sampler of the slot's buffer. Any other
 * selection drops "op1r" (then the stock job: a new type / preset is a stock sound again). ---- */
#define SAVE_SLOT  ((void (*)(void *))0x01940574)              /* job: +4 byte drum, +8 slot */
#define UI_SLOT    ((void (*)(int))0x019C2BB8)
#define UI_SIGNAL  ((void (*)(void))0x019C2F04)
#define MAKE_SAMPLER ((void (*)(int, uint32_t, int))0x01A02A46)   /* patch::make_sampler_patch(slot, base_freq float, end 0..0x7FFF) */
#define SP_C5_F32  0x4402D013u                                  /* 523.25 Hz (C5) as float bits */
static void spx_type_job(void *job)
{
    int slot = *(int32_t *)((uint8_t *)job + 4), sel = *(int32_t *)((uint8_t *)job + 8);
    if ((uint32_t)slot >= SP_SLOTS) { STOCK_TYPE_JOB(job); return; }
    void *json = (uint8_t *)(SYNTH_PATCHES + slot * 0x38) + 4;
    uint32_t tmp[4];
    if (sp_keep_next[slot] && sel <= 0) {                         /* sample browser: the preset's audio and settings,
                                                                     * "op1r" stays (the job copies the patch JSON) */
        sp_keep_next[slot] = 0;
        STOCK_TYPE_JOB(job);
        return;
    }
    if (sel != OP1_SAMPLER_ROW) {
        JV_REMOVE(json, tmp, "op1r"); JV_DTOR(tmp, 2);
        sp_isplus[slot] = 0;
        STOCK_TYPE_JOB(job);
        return;
    }
    if (!jv_is_str(JV_KEY(json, "type"), "sampler"))             /* another type (the browser previews each row
                                                                     * it passes): a sampler patch over the slot's buffer, as after recording */
        MAKE_SAMPLER(slot, SP_C5_F32, 0x7FFF);
    void *op = JV_KEY(json, "op1r");
    if (!(jv_type(op) == JV_OBJECT && jv_is_str(JV_KEY(op, "engine"), "sampler+"))) {
        JV_FROM_STR(tmp, "sampler+");
        JV_ASSIGN(JV_KEY(op, "engine"), tmp);
        JV_DTOR(tmp, 2);
        sp_extra_default(&sp_x[slot]);
        sp_plus_prepare(&sp_x[slot], &sp_c[slot]);
        sp_sync_key[slot][0] = -1;
        sp_fw_store(slot);
    }
    sp_isplus[slot] = 1;
    UI_SLOT(slot);
    UI_SIGNAL();
    struct { uint32_t vt; uint8_t drum, pad[3]; int32_t slot; } save = { 0, 0, { 0, 0, 0 }, slot };
    SAVE_SLOT(&save);
}

static int spx_is_plus(int slot) { return (unsigned)slot < SP_SLOTS && sp_isplus[slot]; }
void spu_ui_build(void), spu_ui_load(void), spu_sync(int editor_open_on_6);   /* sp_ui.c */
void *spu_screen_for(int id);
int spu_knob(int knob, int delta), spu_key(int code, int pressed);
const op1_sampler_t SAMPLERPLUS = { spx_render, spx_note_on, spx_note_off, spx_adsr, spx_slot_loaded, spx_is_plus,
                                    spx_type_job, spu_ui_build, spu_ui_load, spu_screen_for, spu_sync, spu_knob,
                                    spu_key };
