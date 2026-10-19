/* Chord mode core (docs/50): chord / scale / voicing logic and held-key bookkeeping, no firmware calls (host-tested by
 * harness/chord_test.c; the plugin plugins/chord wires it to the player key path and VoiceCtl). */
#ifndef CH_CORE_H
#define CH_CORE_H
#include <stdint.h>

#define CH_MAXN     6              /* notes per chord: up to 4 + bass + one doubled */
#define CH_KEYS     24             /* keyboard keys 0..23 */

enum { CH_FIXED = 0, CH_SCALE = 1 };
/* fixed mode chord types */
enum { CT_MAJ, CT_MIN, CT_SUS2, CT_SUS4, CT_7, CT_MAJ7, CT_MIN7, CT_DIM, CT_AUG, CT_6, CT_ADD9, CT_5, CT_OCT, CT_N };
/* scale mode chord kinds (stacked in the scale) */
enum { SK_TRIAD, SK_7TH, SK_SUS2, SK_SUS4, SK_ADD9, SK_N };
enum { SC_MAJOR, SC_MINOR, SC_DORIAN, SC_MIXO, SC_N };
/* voicings */
enum { VO_CLOSE, VO_INV1, VO_INV2, VO_INV3, VO_DROP2, VO_SPREAD, VO_N };

typedef struct {
    uint8_t mode;                  /* CH_FIXED / CH_SCALE */
    uint8_t type;                  /* fixed: CT_*, scale: SK_* */
    uint8_t root;                  /* scale root 0..11 (C = 0) */
    uint8_t scale;                 /* SC_* */
    uint8_t voicing;               /* VO_* */
    uint8_t bass;                  /* 1: the chord's root an octave below the lowest note */
} ch_cfg;

/* the chord for one played note (MIDI): notes ascending, returns the count (0..CH_MAXN) */
int ch_notes(const ch_cfg *c, int midi, uint8_t *out);
/* name of the chord ch_notes gives for midi ("Cmin7/G", "D", "F#dim"; "min", not "m": the stock font shows capitals
 * only), into s (>= 16 bytes) */
void ch_name(const ch_cfg *c, int midi, char *s);

/* held keys: which notes each key turned on; a note shared by two held chords sounds until both are released */
typedef struct {
    uint8_t n[CH_KEYS], notes[CH_KEYS][CH_MAXN];
    uint8_t count[128];            /* how many held keys hold this note */
} ch_held;
typedef void (*ch_out_fn)(int note, int vel, int on, void *ctx);
/* key down: notes = the chord's notes (drum mode: pads, see ch_drum_fold); calls out(note, vel, 1) for notes not
 * yet sounding. A second down of a held key releases it first. */
void ch_down(ch_held *h, int key, const uint8_t *notes, int n, int vel, ch_out_fn out, void *ctx);
void ch_up(ch_held *h, int key, ch_out_fn out, void *ctx);
/* one more note for a held key (strum: the chord's notes one after the other); 0 if the key's list is full */
int ch_add(ch_held *h, int key, int note, int vel, ch_out_fn out, void *ctx);
void ch_all_off(ch_held *h, ch_out_fn out, void *ctx);
/* drum mode: chord notes -> pad keys 0..23 (key + interval, folded back by octaves), duplicates removed */
int ch_drum_fold(int key, int midi, const uint8_t *notes, int n, uint8_t *pads);

#endif
