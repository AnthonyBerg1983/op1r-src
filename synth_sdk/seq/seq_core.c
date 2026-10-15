/* Core part of the sequencers (the sequencers themselves are plugin files, docs/44): what they and other core code
 * share. The one firmware copy of seq_text.h (stroke text, lines, oracle / peal DB persistence; exported to plugins),
 * the VoiceCtl note output of euclid / dots / oracle / peal, and the "start with REC held" flag dispatcher.c turns
 * into a PLAY press. */
#include "../op1_synth.h"
#define OCC_TEXT_IMPL
#include "seq_text.h"

volatile uint8_t eu_rec_play;                           /* Shift + C# started a sequencer with REC held */

/* notes go through VoiceCtl like the stock sequencers (ok_player). A slot stores the absolute pitch taken when its
 * key was pressed (key + 0x35 + octave * 12), so later octave changes do not move it.
 *  synth: record {midi, vel, send midi = 1, 0} -> key_down 0x01A85750 -> note_on_synth (uses midi as is).
 *  drums: note_on_drum takes the SOUND from record midi - 0x35 (key 0..23) and the PITCH (semitones) from the
 *         param, to which key_down adds the current drum octave * 12 -> param = pitch - current drum octave * 12. */
#define VC_DOWN   ((void (*)(int32_t *, int))0x01A85750)
#define VC_UP     ((void (*)(int32_t *))0x01A85840)
#define DRUM_OCT()  (((const int *)0x01DFE500)[*(volatile int *)0xFF8016D8 & 7])
int eu_fw_note_on(int pos, int note, int key, int vel)
{
    int midi = pos ? key + 0x35 : note, param = pos ? note - 0x35 - DRUM_OCT() * 12 : 0;
    if (midi < 0 || midi > 127) return -1;
    int32_t n[4] = { midi, vel, 0, 0 };
    ((uint8_t *)n)[8] = 1;
    VC_DOWN(n, param);
    return midi;                                                   /* note-off uses this */
}
void eu_fw_note_off(int midi)
{
    int32_t n[4] = { midi, 0, 1, 0 };
    ((uint8_t *)n)[8] = 1;
    VC_UP(n);
}
