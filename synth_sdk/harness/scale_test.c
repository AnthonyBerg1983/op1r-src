/* Host test for scale/sc_core.c (docs/55): mapping modes, held notes across scale changes, shared notes. */
#include <assert.h>
#include <stdio.h>
#include "../scale/sc_core.c"

int main(void)
{
    sc_type = SC_OFF;
    for (int n = 0; n < 128; n++) assert(sc_map(n) == n);
    sc_type = 1; sc_root = 0; sc_mode = SC_SNAP;                     /* C major, snap */
    assert(sc_map(60) == 60 && sc_map(61) == 60 && sc_map(63) == 62 && sc_map(66) == 65 && sc_map(71) == 71);
    sc_root = 2;                                                     /* D major: C# in, C -> B */
    assert(sc_map(61) == 61 && sc_map(60) == 59 && sc_map(0) == -1);
    assert(sc_map(1) == 1 && sc_map(0) == -1);                       /* below 0: silent */
    sc_root = 0; sc_mode = SC_MUTE;
    assert(sc_map(61) == -1 && sc_map(64) == 64);
    sc_mode = SC_WHITE; sc_type = 2;                                  /* C minor on white keys */
    assert(sc_map(60) == 60 && sc_map(62) == 62 && sc_map(64) == 63 && sc_map(69) == 68 && sc_map(71) == 70);
    assert(sc_map(72) == 72 && sc_map(48) == 48 && sc_map(61) == -1);
    sc_root = 9;                                                     /* A minor: C plays A */
    assert(sc_map(60) == 69 && sc_map(62) == 71 && sc_map(64) == 72);
    sc_type = 11; sc_root = 0;                                       /* minor pentatonic: 5 degrees per 5 white keys */
    assert(sc_map(60) == 60 && sc_map(62) == 63 && sc_map(64) == 65 && sc_map(65) == 67 && sc_map(67) == 70 && sc_map(69) == 72);
    assert(sc_map(59) == 58 && sc_map(57) == 55);                    /* below middle C */
    for (int n = 0; n < 128; n++) { int m = sc_map(n); assert(m >= -1 && m <= 127); }
    for (int t = 1; t < SC_NTYPES; t++) for (int md = 0; md < SC_NMODES; md++) for (int r = 0; r < 12; r += 5) {
        sc_type = (uint8_t)t; sc_mode = (uint8_t)md; sc_root = (uint8_t)r;          /* unmap: a key that plays the note */
        for (int n = 24; n < 104; n++) { int m = sc_map(n); if (m >= 0) assert(sc_map(sc_unmap(m)) == m); }
    }
    sc_type = 1; sc_root = 0; sc_mode = SC_WHITE; assert(sc_unmap(62) == 62 && sc_map(64) == 64);
    sc_type = 2; assert(sc_unmap(63) == 64);                        /* C minor white keys: Eb comes from E */
    /* held notes */
    sc_type = 1; sc_root = 0; sc_mode = SC_SNAP;
    assert(sc_down(61, 1) == 60);                                    /* C# -> C */
    sc_type = 0;                                                     /* scale off while held */
    assert(sc_up(61) == 60);                                         /* releases the C it played */
    sc_type = 1;
    assert(sc_down(60, 1) == 60 && sc_down(61, 1) == 60);           /* two keys on C */
    assert(sc_up(60) == -1 && sc_up(61) == 60);                      /* C ends with the last key */
    assert(sc_down(61, 0) == 61 && sc_up(61) == 61);                 /* not mapped caller: as is */
    sc_mode = SC_MUTE; assert(sc_down(61, 1) == -1 && sc_up(61) == -1);
    assert(sc_down(62, 1) == 62 && sc_up(62) == 62);
    for (int n = 0; n < 128; n++) assert(held[n] == -2 && !ocnt[n] && !mcnt[n]);
    printf("ok\n");
    return 0;
}
