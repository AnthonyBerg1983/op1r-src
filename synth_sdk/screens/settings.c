/* Settings page (clock / card + T3, docs/25), in the style of the playmode page like the USB out page: blue
 * "COUNT-IN" with 4 / 8 / 16 beats (tape/countin.c). Blue turns through the values; saved in op1rsettings
 * (key 'countin'). The other knob rows are free for future small settings. */
#include "../op1_synth.h"
#include "stroke_font.h"

extern uint8_t ci_beats;
static const uint8_t ci_vals[3] = { 4, 8, 16 };
static uint8_t ci_idx;                                      /* index into ci_vals */

#ifdef OP1_HOST
int settings_host_saved = -1;
static void st_load(void) { if (settings_host_saved >= 0) ci_idx = (uint8_t)settings_host_saved; }
static void st_save(void) { settings_host_saved = ci_idx; }
#else
#define DB_HANDLE ((void *(*)(void))0x0196DD44)
#define DB_EXEC   ((void (*)(void *, const char *))0x0196F950)
#define DB_GET    ((void (*)(void *, void *, const char *))0x0196FBD0)
typedef struct { uint32_t alloc; union { char buf[16]; char *ptr; } u; uint32_t size, cap; uint32_t pad; } st_dstr;
static const char ST_CREATE[] = "CREATE TABLE IF NOT EXISTS op1rsettings (key TEXT PRIMARY KEY, val INTEGER NOT NULL)";
static void st_load(void)                                   /* boot, screen init */
{
    void *db = DB_HANDLE();
    DB_EXEC(db, ST_CREATE);
    st_dstr s = { 0 };
    DB_GET(&s, db, "SELECT val FROM op1rsettings WHERE key='countin'");
    const char *p = s.cap > 15 ? s.u.ptr : s.u.buf;
    if (s.size && p[0] >= '0' && p[0] <= '2') ci_idx = (uint8_t)(p[0] - '0');
}
static void st_save(void)                                   /* KeyHandler thread, on change */
{
    static char q[] = "INSERT OR REPLACE INTO op1rsettings VALUES ('countin',0)";
    q[sizeof q - 3] = (char)('0' + ci_idx);
    void *db = DB_HANDLE();
    DB_EXEC(db, ST_CREATE);
    DB_EXEC(db, q);
}
#endif

static int32_t acc;
void settings_turn(int knob, int delta)                     /* blue: one value per detent, clamped */
{
    if (knob != 0) return;
    if ((acc > 0 && delta < 0) || (acc < 0 && delta > 0)) acc = 0;
    acc += delta;
    int st = acc / 1024; if (!st) return;
    acc = 0;
    int v = ci_idx + (st > 0 ? 1 : -1);
    if (v < 0 || v > 2) return;
    ci_idx = (uint8_t)v; ci_beats = ci_vals[ci_idx];
    st_save();
}

static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF };
#define ST_S   OP1_Q10(5) / 2                               /* as the USB out page: 15 px caps */
#define ST_W   0x600
#define ST_LX  OP1_Q10(24)
#define ST_VX  OP1_Q10(164)
#define ST_Y0  OP1_Q10(40)

static void st_init(void *scr) { (void)scr; st_load(); ci_beats = ci_vals[ci_idx]; }
static void st_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    static const char *const txt[3] = { "4", "8", "16" };
    sf_text(r, ST_LX, ST_Y0, ST_S, "COUNT-IN", ST_W, c_blue);
    sf_text(r, ST_VX, ST_Y0, ST_S, txt[ci_idx], ST_W, c_blue);
}
const op1_screen_t SCREEN_settings = { st_init, st_update };
