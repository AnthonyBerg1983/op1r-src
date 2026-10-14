/* screens/guirate.c rate policy: idle -> every 5th tick, input -> capped rate for 5 s, sound / tape / a changed
 * frame -> capped rate for 1 s, skipped time goes to the next render, caps 60 / 30 / 20 / 12 fps, frame checksum.
 *   cc -DOP1_HOST -I../.. guirate_test.c   (from shelved/guirate) */
#include "guirate.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int host_gui_input, host_gui_tape, host_gui_peak;

static int run(int ticks, int changed_every)                 /* renders in `ticks`; a changed frame every n renders */
{
    int r = 0;
    for (int t = 0; t < ticks; t++)
        if (gui_should_render(16, changed_every && r % changed_every == 0 && r)) { r++; gui_take_dt(); }
    return r;
}

int main(void)
{
    run(600, 0);                                             /* settle: idle */
    int r = 0;
    for (int t = 0; t < 400; t++) if (gui_should_render(16, 0)) { r++; assert(gui_take_dt() == 16 * 5); }
    assert(r == 80);                                         /* idle: 12 fps, skipped time handed on */
    gui_input = 1; assert(run(300, 0) == 300);               /* input: full rate for 5 s */
    assert(run(100, 0) == 20);                               /* then idle again */
    host_gui_peak = 500; assert(run(200, 0) == 200); host_gui_peak = 0;   /* sound */
    run(120, 0);
    host_gui_tape = 2; assert(run(200, 0) == 200); host_gui_tape = 0;     /* tape running */
    run(120, 0);
    assert(run(600, 1) >= 590);                              /* every frame changes: stays at full rate */
    run(120, 0);
    gui_cap = 1; assert(run(600, 1) >= 290); gui_cap = 0;     /* 30 fps cap, every frame changes: stays at 30 */
    run(120, 0);
    { int r = 0;                                             /* animation whose change the checksum misses on every
                                                                4th frame: no drop to the idle rate */
      for (int t = 0; t < 600; t++) if (gui_should_render(16, r > 0 && r % 4 != 0)) { r++; gui_take_dt(); }
      assert(r >= 590); }
    run(120, 0);
    { int r = 0;                                             /* a change every 30 renders, seen twice (both buffers):
                                                                mostly idle (the resource monitor) */
      for (int t = 0; t < 600; t++) if (gui_should_render(16, r && (r % 30 == 0 || r % 30 == 1))) { r++; gui_take_dt(); }
      assert(r < 200); }
    run(120, 0);
    static const int per[4] = { 1, 2, 3, 5 };
    for (int c = 0; c < 4; c++) {                            /* caps with input: 60 / 30 / 20 / 12 fps */
        gui_cap = (uint8_t)c;
        int n = 0;
        for (int t = 0; t < 300; t++) { gui_input = 1; if (gui_should_render(16, 0)) { n++; gui_take_dt(); } }
        assert(n >= 300 / per[c] - 1 && n <= 300 / per[c] + 1);
    }
    gui_cap = 0;
    int sum = 0, given = 0;                                  /* time conservation */
    for (int t = 0; t < 1000; t++) { given += 7; if (gui_should_render(7, 0)) sum += gui_take_dt(); }
    sum += gui_take_dt();
    assert(sum == given);
    static uint8_t fb[198 * 1024];                           /* checksum: equal frames equal, a changed pixel differs */
    uint32_t a = gui_frame_sum(fb); fb[4 * 1024 + 16 * 4] = 1; assert(gui_frame_sum(fb) != a);
    printf("OK: gui rate policy (stats %u rendered / %u skipped)\n", gui_stat[0], gui_stat[1]);
    return 0;
}
