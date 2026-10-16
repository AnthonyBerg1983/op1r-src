/* MIDI sequencer view drawing (ms_draw.c): everything the frame needs, gathered by the caller */
#ifndef MS_DRAW_H
#define MS_DRAW_H
#include "ms_core.h"
typedef struct {
    int32_t ph, zoom_q10, bpm_q8;           /* playhead (samples), tape zoom (Q10 px per second), tempo (Q8 bpm) */
    int loop_on; int32_t loop_in, loop_out;
    int32_t sel_a, sel_b;                   /* selection (-1: none) */
    int shift;                              /* Shift held: the selection covers all tracks */
    int quant;                              /* quantize grid MS_Q_* (status bar) */
    int io;                                 /* white knob edits: 0 output, 1 input */
    int sel_track, rec, punch;              /* selected track, recording, tape punch mode (markers red) */
    int tm_n; const int32_t *tm;            /* tape markers (tape/markers.c) */
} ms_view;
void ms_draw(void *r, const ms_view *v, ms_t *s);
#endif
