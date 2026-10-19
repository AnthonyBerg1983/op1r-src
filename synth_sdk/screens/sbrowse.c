/* Sample browser for synth engines that play the slot's sample (plugin.json "uses_sample", e.g. granular): a press
 * set to OP1_PRESS_BROWSE on the engine screen opens it, as the sampler+ browser (samplerplus/sp_ui.c, docs/46): the
 * sampler presets (factory + user), white turn scrolls, white press loads, red press / T1 closes. Loading runs the
 * stock synth type job (sampler preset: the patch becomes a sampler, its audio is loaded into the slot's buffer and
 * copied to synth_<slot>.raw); sb_job puts the engine's patch back right after (docs/49). */
#include "../op1_synth.h"

extern const op1_engine_t *op1_engines[];

#define SLOT_CUR        (*(volatile int *)0xFF8016D4 & 7)
#define SYNTH_PATCHES   0x01DDD7E8                                        /* 8 synth patch objects x 0x38, JSON at +4 */
#define PRESETS         0x01DDDB68                                        /* preset table, 0x38 each (JSON at +4) */
#define PRESET_N        (*(const volatile int32_t *)0x018B6114)
#define QUEUE_TYPE_JOB  ((void (*)(int, int))0x01940FD0)                /* (selection, slot): synth type job */
#define PATCH_JSON_SET  ((void (*)(void *, const void *))0x019FF5F0)    /* patch obj, JSON (as the type job) */
#define UI_SLOT         ((void (*)(int))0x019C2BB8)
#define UI_SIGNAL       ((void (*)(void))0x019C2F04)
#define SAVE_SLOT       ((void (*)(void *))0x01940574)                  /* job: +4 byte drum, +8 slot (DB row) */
#define JV_CGET   ((const void *(*)(const void *, const char *))0x019BEB70)
#define JV_ASSIGN ((void *(*)(void *, const void *))0x019BDDC0)
#define JV_NULL_C ((void *(*)(void *, int))0x019BFA30)
#define JV_DTOR   ((void (*)(void *, int))0x019BDD18)

#define TS 12
#define Q(v) ((v) << 10)
#define BR_MAX 96
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_grey[4] = { 0x6F, 0x71, 0x73, 0xFF },
                     c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF }, c_sel[4] = { 0x00, 0x59, 0x3A, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_red };

static uint8_t sb_open_n;                       /* engine number whose browser is open, 0: closed */
static int16_t br_idx[BR_MAX], br_n, br_sel;
static int32_t acc;
static volatile uint8_t sb_keep[8];             /* per slot: the next sampler-preset type job keeps the engine */

static const char *jv_str(const void *json, const char *key)
{
    const void *v = JV_CGET(json, key);
    return v && ((const uint8_t *)v)[8] == 4 ? *(const char *const *)v : 0;
}
static int str_eq(const char *a, const char *b) { if (!a) return 0; while (*a && *a == *b) a++, b++; return *a == *b; }
static void br_scan(void)
{
    br_n = 0;
    int n = PRESET_N;
    for (int i = 0; i < n && br_n < BR_MAX; i++)
        if (str_eq(jv_str((const uint8_t *)(PRESETS + i * 0x38) + 4, "type"), "sampler")) br_idx[br_n++] = (int16_t)i;
    if (br_sel >= br_n) br_sel = 0;
}
static const char *br_name(int k)
{
    const char *s = k >= 0 && k < br_n ? jv_str((const uint8_t *)(PRESETS + br_idx[k] * 0x38) + 4, "name") : 0;
    return s ? s : "?";
}

/* key on the engine screen of engine n (0: not shown); 1 = handled */
int sb_key(int n, int code, int pressed)
{
    if (!n) { sb_open_n = 0; return 0; }
    if (sb_open_n && sb_open_n != n) sb_open_n = 0;
    if (sb_open_n && code == 0x06) { if (pressed) sb_open_n = 0; return 1; }   /* T1 closes */
    if (code < 0x3B || code > 0x3E) return 0;
    int k = code - 0x3B;
    if (!sb_open_n) {
        if (op1_engines[n]->press[k] != OP1_PRESS_BROWSE) return 0;
        if (pressed) { br_scan(); acc = 0; sb_open_n = (uint8_t)n; }
        return 1;
    }
    if (!pressed) return 1;
    if (k == 2 && br_n) {                                            /* white press: load (the engine stays) */
        int slot = SLOT_CUR;
        sb_keep[slot] = 1;
        QUEUE_TYPE_JOB(-br_idx[br_sel], slot);
        sb_open_n = 0;
    } else if (k == 3) sb_open_n = 0;
    return 1;
}

int sb_knob(int n, int knob, int delta)                              /* while open: white scrolls, others ignored */
{
    if (!n || sb_open_n != n || knob < 0 || knob > 3) return 0;
    if (knob == 2) {
        acc += delta;
        int st = acc / 1024; acc -= st * 1024;
        br_sel = (int16_t)(br_sel + st < 0 ? 0 : br_sel + st >= br_n ? br_n - 1 : br_sel + st);
    }
    return 1;
}

/* ---- drawing (GUI thread): instead of the engine's screen while open ---- */
enum { TX_TITLE, TX_LAB, TX_VAL, TX_LOAD, TX_BACK, TX_ROW, TX_N = TX_ROW + 5 };
static uint32_t tx[TX_N][OP1_TEXT_SIZE / 4];
static char shown[TX_N][14];
static uint8_t tx_made;
static void ln(void *r, int x0, int y0, int x1, int y1, int w, const uint8_t *c)
{
    if (x0 == x1 && y0 == y1) return;
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, w, c);
}
static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    if (!s || !s[0]) return;
    int same = 1;
    for (int j = 0; j < 14; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 13 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
    op1_text_draw(tx[i], r, ident);
}
static char *put_n(char *p, int v) { char b[8]; int n = 0; do b[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 7); while (n) *p++ = b[--n]; return p; }

int sb_draw(int n)                                                   /* 1: drawn (the engine's screen skipped) */
{
    if (!n || sb_open_n != n) return 0;
    if (!tx_made) {                                                  /* as sp_ui.c's texts */
        op1_text_ctor(tx[TX_TITLE], Q(8), 4, TS, c_white);
        op1_text_ctor(tx[TX_LAB], Q(8 + 78 * 2), 114, TS, c_white);
        op1_text_ctor(tx[TX_VAL], Q(8 + 78 * 2), 127, TS, c_white);
        op1_text_ctor(tx[TX_LOAD], Q(8 + 78 * 2), 94, TS, c_white);
        op1_text_ctor(tx[TX_BACK], Q(8 + 78 * 3), 94, TS, c_red);
        for (int i = 0; i < 5; i++) op1_text_ctor(tx[TX_ROW + i], Q(22), 20 + 15 * i, TS, c_green);
        tx_made = 1;
    }
    void *r = op1_raster();
    for (int k = 0; k < 4; k++) ln(r, Q(8 + 78 * k), Q(110), Q(78 + 78 * k), Q(110), 0x800, col4[k]);
    txt(r, TX_TITLE, "samples");
    int first = br_sel - 2; if (first > br_n - 5) first = br_n - 5; if (first < 0) first = 0;
    for (int i = 0; i < 5 && first + i < br_n; i++) {
        if (first + i == br_sel) ln(r, Q(18), Q(26 + 15 * i), Q(168), Q(26 + 15 * i), 0x3400, c_sel);   /* 13 px bar */
        txt(r, TX_ROW + i, br_name(first + i));
    }
    if (br_n > 5) {                                                  /* scroll bar */
        int y0 = 20 + 72 * first / br_n, y1 = 20 + 72 * (first + 5) / br_n;
        ln(r, Q(180), Q(20), Q(180), Q(92), 0x400, c_grey);
        ln(r, Q(180), Q(y0), Q(180), Q(y1), 0x800, c_green);
    }
    char v[16], *p = put_n(v, br_n ? br_sel + 1 : 0);
    *p++ = '/'; *put_n(p, br_n) = 0;
    txt(r, TX_LAB, "sample"); txt(r, TX_VAL, v); txt(r, TX_LOAD, "load"); txt(r, TX_BACK, "back");
    return 1;
}

/* ---- the type job (job thread, sp_core.c sp_type_job): a browser load keeps the engine's patch ---- */
int sb_job(void *job, void (*type_job)(void *))
{
    int slot = *(int32_t *)((uint8_t *)job + 4), sel = *(int32_t *)((uint8_t *)job + 8);
    if ((unsigned)slot >= 8 || !sb_keep[slot]) return 0;
    sb_keep[slot] = 0;
    if (sel > 0) return 0;
    uint8_t *patch = (uint8_t *)SYNTH_PATCHES + slot * 0x38;
    uint32_t saved[4];
    JV_NULL_C(saved, 0); JV_ASSIGN(saved, patch + 4);                /* the engine's patch */
    type_job(job);                                                   /* audio -> buffer + synth_<slot>.raw (patch: sampler) */
    PATCH_JSON_SET(patch, saved);                                    /* the engine's patch again */
    JV_DTOR(saved, 2);
    UI_SLOT(slot);
    UI_SIGNAL();
    struct { uint32_t vt; uint8_t drum, pad[3]; int32_t slot; } save = { 0, 0, { 0, 0, 0 }, slot };
    SAVE_SLOT(&save);                                                /* DB row: the engine's patch */
    return 1;
}
