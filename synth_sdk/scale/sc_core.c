/* Scale mode (docs/55): notes played into VoiceCtl from the keyboard and the stock sequencers are mapped to a scale
 * (synth mode only). Our own players (euclid, dots, chord, grids, MIDI sequencer: callers in our code) and received
 * MIDI are not mapped, keys a player consumes never get here. No firmware calls: shared by the host test.
 * Held notes: the mapping made at key down is kept per played note, so the key up releases what was played even if
 * the scale changed meanwhile; notes that two keys map onto sound until both are up. */
#include "sc_core.h"

uint8_t sc_root, sc_type, sc_mode;                          /* root 0..11, scale SC_OFF.., mode SC_SNAP.. */

static const uint16_t masks[SC_NTYPES] = {                  /* bit n = semitone n above the root */
    0xFFF,                                                  /* off (chromatic) */
    0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B,        /* major minor dorian phrygian lydian mixolydian locrian */
    0x9AD, 0xAAD,                                           /* harmonic minor, melodic minor */
    0x295, 0x4A9, 0x4E9 };                                  /* major pentatonic, minor pentatonic, blues */
const char *const sc_type_names[SC_NTYPES] = { "OFF", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN",
    "LOCRIAN", "HARM MINOR", "MEL MINOR", "PENTA MAJOR", "PENTA MINOR", "BLUES" };
const char *const sc_mode_names[SC_NMODES] = { "SNAP", "WHITE KEYS", "MUTE" };
const char *const sc_root_names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

uint16_t sc_mask(void) { return masks[sc_type < SC_NTYPES ? sc_type : 0]; }

int sc_map(int note)                                        /* -1 = silent */
{
    if (note < 0 || note > 127) return note;
    uint16_t m = sc_mask();
    if (m == 0xFFF) return note;
    int pc = (note - sc_root + 120) % 12;
    if (sc_mode == SC_MUTE) return m >> pc & 1 ? note : -1;
    if (sc_mode == SC_WHITE) {
        static const int8_t widx[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };   /* white key index from C */
        int w = widx[note % 12];
        if (w < 0) return -1;
        int8_t deg[12]; int n = 0;
        for (int i = 0; i < 12; i++) if (m >> i & 1) deg[n++] = (int8_t)i;
        int d = note / 12 * 7 + w - 35;                     /* white keys from middle C (60): C plays the root there */
        int q = d >= 0 ? d / n : -((-d + n - 1) / n);       /* floor */
        int out = 60 + sc_root + q * 12 + deg[d - q * n];
        return out >= 0 && out <= 127 ? out : -1;
    }
    int d = 0;                                              /* snap: the scale note at or below */
    while (!(m >> ((pc - d + 12) % 12) & 1)) d++;
    note -= d;
    return note >= 0 ? note : -1;
}

int sc_unmap(int note)                                      /* a note that sc_map turns into `note` (nearest), else note */
{
    if (note < 0 || note > 127 || sc_map(note) == note) return note;
    for (int d = 1; d < 128; d++) {                             /* white keys: fewer degrees spread wider */
        if (note - d >= 0 && sc_map(note - d) == note) return note - d;
        if (note + d <= 127 && sc_map(note + d) == note) return note + d;
    }
    return note;
}

/* per played note: its mapping (-1 silent, -2 not held) and how many downs are open; per mapped note: open downs */
static int8_t held[128] = { [0 ... 127] = -2 };
static uint8_t ocnt[128], mcnt[128];

int sc_down(int note, int mapped_ok)                        /* mapped_ok: this caller / mode is mapped; returns the
                                                               note to play, -1 = do not play */
{
    if (note < 0 || note > 127) return note;
    if (ocnt[note]) { if (ocnt[note] < 255) ocnt[note]++; int m = held[note]; if (m >= 0 && mcnt[m] < 255) mcnt[m]++; return m; }
    int m = mapped_ok ? sc_map(note) : note;
    if (!mapped_ok || m == note) {
        if (mcnt[note] == 0) return note;                   /* plain note, nothing to track (the common case) */
    }
    held[note] = (int8_t)m; ocnt[note] = 1;
    if (m >= 0 && mcnt[m] < 255) mcnt[m]++;
    return m;
}

int sc_up(int note)                                         /* the note to release, -1 = none */
{
    if (note < 0 || note > 127) return note;
    if (!ocnt[note]) {                                      /* not tracked: played as is */
        if (mcnt[note]) return -1;                          /* .. but a mapped key still holds that note */
        return note;
    }
    int m = held[note];
    if (--ocnt[note]) return -1;
    held[note] = -2;
    if (m < 0) return -1;
    if (mcnt[m]) mcnt[m]--;
    return mcnt[m] ? -1 : m;
}
