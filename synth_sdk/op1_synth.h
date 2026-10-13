/* op1r core header (private): the plugin API (op1_plugin.h) plus firmware helpers used only by core code. */
#ifndef OP1_SYNTH_H
#define OP1_SYNTH_H
#include "op1_plugin.h"

/* ---- writing long SQL statements safely ----
 * db2::exec (0x0196F950) asserts on any sqlite error (0x01919AA8, "db2.cpp:165 %s\n%s", bounded to 256 bytes). The
 * assert's stack printer (0x0191995C) follows FP as a frame chain; our -O2 code uses FP as a normal register (here:
 * the dots SQL buffer), so it read SQL text as a frame and faulted (user report: SEQSTAT 0x24 at 0x019199C0) instead
 * of showing the assert. The sqlite error itself was "out of memory" (fixed heap, page cache never capped: see
 * sqlite_cache_hook in dispatcher.c). This calls sqlite3_exec directly under db2's own lock and returns the error
 * code instead of asserting. op1_db_last_err keeps the last nonzero code (shown on the monitor page). */
extern int32_t op1_db_last_err;
#ifndef OP1_HOST
static inline int op1_db_write(void *db2_handle, const char *sql)
{
    void *sem = *(void *volatile *)0x01BAB718;                    /* db2 statement lock (created on the first exec) */
    if (sem) ((void (*)(void *, int))0xFFA0B6E8)(sem, 0);
    int rc = ((int (*)(void *, const char *, void *, void *, char **))0x019C9460)(*(void **)db2_handle, sql, 0, 0, 0);
    if (sem) ((void (*)(void *))0xFFA0B96C)(sem);
    if (rc) op1_db_last_err = rc;
    return rc;
}
#endif

#ifndef OP1_HOST
/* firmware text (stock font, docs/18): object of OP1_TEXT_SIZE bytes, built once (GUI thread, after boot);
 * x in Q10, y in px, size 12 = the battery page's text (template: BatteryScreen 0x0193D248 / draw 0x0193CFD8) */
#define OP1_TEXT_SIZE       0xA4
#define op1_text_ctor(obj, x_q10, y, size, rgba) \
    (((void *(*)(void *, int32_t, int32_t, int32_t, const uint8_t *, int32_t))0x01A61904)((obj), (x_q10), (y), (size), (rgba), 0))
#define op1_text_set(t, str) (((void (*)(void *, const char *))0x01A61AAC)((t), (str)))
#define op1_text_draw(t, r, m9) (((void (*)(void *, void *, const int32_t *))0x01A6259C)((t), (r), (m9)))
#endif

#endif
