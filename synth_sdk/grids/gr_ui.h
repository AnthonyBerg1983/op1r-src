/* grids sequencer page state and controls (docs/52), see gr_ui.c */
#ifndef GR_UI_H
#define GR_UI_H
#include "gr_core.h"

typedef struct {
    gr_t    g;                    /* settings + this bar's chaos */
    uint8_t sel;                  /* selected part (white) */
    uint8_t note[GR_PARTS];       /* drum: pad key 0..23; synth: midi note (used with no key held) */
    uint8_t swing;                /* 0..42 (Grids' range) */
    uint8_t res32;                /* 0: 1/16 (even steps), 1: 1/32 */
    uint8_t accent;               /* velocity difference of accents, 0..127 */
    uint8_t playing, step;
} gr_bank;
extern gr_bank gr_b[2];

void gr_ui_init(int m);
void gr_ui_turn(int m, int knob, int detents, int shift);
void gr_ui_press(int m, int knob);
int  gr_ui_step(int m, int step);
void gr_ui_draw(void *r, int m, int shift);
#endif
