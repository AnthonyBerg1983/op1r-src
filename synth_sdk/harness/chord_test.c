/* Host test of chord/ch_core.c (docs/50).
 *   cc -Wall -Wextra -fsanitize=address,undefined -I.. chord_test.c ../chord/ch_core.c -o build/chord_test && build/chord_test
 *   build/chord_test table        prints every chord type / scale degree for a look */
#include "../chord/ch_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int notes_are(const ch_cfg *c, int midi, const char *want)          /* "60 64 67" */
{
    uint8_t o[CH_MAXN];
    int n = ch_notes(c, midi, o);
    char got[64] = "", *p = got;
    for (int i = 0; i < n; i++) p += sprintf(p, i ? " %d" : "%d", o[i]);
    if (strcmp(got, want)) { printf("  notes of %d: %s, want %s\n", midi, got, want); return 0; }
    return 1;
}
static int name_is(const ch_cfg *c, int midi, const char *want)
{
    char s[16];
    ch_name(c, midi, s);
    if (strcmp(s, want)) { printf("  name of %d: %s, want %s\n", midi, s, want); return 0; }
    return 1;
}

/* note output log for the held-key tests */
static char log_[512];
static void out(int note, int vel, int on, void *ctx) { (void)ctx; (void)vel; sprintf(log_ + strlen(log_), "%c%d ", on ? '+' : '-', note); }

int main(int argc, char **argv)
{
    ch_cfg c = { CH_FIXED, CT_MAJ, 0, SC_MAJOR, VO_CLOSE, 0 };
    if (argc > 1 && !strcmp(argv[1], "table")) {
        static const char *tn[CT_N] = { "maj", "min", "sus2", "sus4", "7", "maj7", "min7", "dim", "aug", "6", "add9", "5", "oct" };
        for (int t = 0; t < CT_N; t++) {
            c.type = (uint8_t)t; uint8_t o[CH_MAXN]; int n = ch_notes(&c, 60, o); char s[16]; ch_name(&c, 60, s);
            printf("fixed %-5s %-8s", tn[t], s); for (int i = 0; i < n; i++) printf(" %d", o[i]); printf("\n");
        }
        c.mode = CH_SCALE;
        for (int k = 0; k < SK_N; k++) {
            c.type = (uint8_t)k; printf("scale kind %d:", k);
            for (int d = 0; d < 12; d++) { char s[16]; ch_name(&c, 60 + d, s); printf(" %s", s); }
            printf("\n");
        }
        return 0;
    }
    /* fixed chords on C4 (60) and their names */
    CHECK(notes_are(&c, 60, "60 64 67") && name_is(&c, 60, "C"), "C major");
    c.type = CT_MIN7; CHECK(notes_are(&c, 62, "62 65 69 72") && name_is(&c, 62, "Dmin7"), "Dmin7");
    c.type = CT_7;    CHECK(notes_are(&c, 67, "67 71 74 77") && name_is(&c, 67, "G7"), "G7");
    c.type = CT_DIM;  CHECK(name_is(&c, 71, "Bdim"), "Bdim");
    c.type = CT_ADD9; CHECK(notes_are(&c, 60, "60 64 67 74") && name_is(&c, 60, "Cadd9"), "Cadd9");
    c.type = CT_5;    CHECK(notes_are(&c, 60, "60 67") && name_is(&c, 60, "C5"), "power chord");
    /* voicings */
    c.type = CT_MAJ;
    c.voicing = VO_INV1;  CHECK(notes_are(&c, 60, "64 67 72") && name_is(&c, 60, "C/E"), "1st inversion");
    c.voicing = VO_INV2;  CHECK(notes_are(&c, 60, "67 72 76") && name_is(&c, 60, "C/G"), "2nd inversion");
    c.type = CT_MAJ7;
    c.voicing = VO_DROP2; CHECK(notes_are(&c, 60, "55 60 64 71"), "drop 2 (G below)");
    c.type = CT_MAJ;
    c.voicing = VO_SPREAD; CHECK(notes_are(&c, 60, "48 67 76"), "spread");
    c.voicing = VO_CLOSE; c.bass = 1; CHECK(notes_are(&c, 60, "48 60 64 67"), "bass note");
    c.bass = 0;
    /* scale mode: C major, every key gives the chord of its degree */
    c.mode = CH_SCALE; c.type = SK_TRIAD; c.root = 0; c.scale = SC_MAJOR;
    static const char *const cmaj[7] = { "C", "Dmin", "Emin", "F", "G", "Amin", "Bdim" };
    static const int white[7] = { 60, 62, 64, 65, 67, 69, 71 };
    for (int d = 0; d < 7; d++) CHECK(name_is(&c, white[d], cmaj[d]), "C major degree %d", d + 1);
    CHECK(notes_are(&c, 69, "69 72 76"), "Am notes");
    CHECK(name_is(&c, 61, "C"), "C# outside C major snaps down to C");
    c.type = SK_7TH;
    static const char *const cmaj7[7] = { "Cmaj7", "Dmin7", "Emin7", "Fmaj7", "G7", "Amin7", "Bmin7b5" };
    for (int d = 0; d < 7; d++) CHECK(name_is(&c, white[d], cmaj7[d]), "C major 7th degree %d", d + 1);
    c.type = SK_TRIAD; c.root = 9; c.scale = SC_MINOR;                    /* A minor: same chords as C major */
    for (int d = 0; d < 7; d++) CHECK(name_is(&c, white[d], cmaj[d]), "A minor = C major degree %d", d + 1);
    c.root = 2; c.scale = SC_DORIAN;                                      /* D dorian: also the white keys */
    for (int d = 0; d < 7; d++) CHECK(name_is(&c, white[d], cmaj[d]), "D dorian degree %d", d + 1);
    c.root = 7; c.scale = SC_MIXO; CHECK(name_is(&c, 67, "G") && name_is(&c, 65, "F"), "G mixolydian");
    c.root = 0; c.scale = SC_MAJOR;
    for (int k = 0; k < SK_N; k++) {                                      /* every degree of every kind has a name */
        c.type = (uint8_t)k;
        for (int d = 0; d < 12; d++) { char s[16]; ch_name(&c, 60 + d, s); CHECK(!strchr(s, '?'), "kind %d degree %d: %s", k, d, s); }
    }
    /* range: notes outside 0..127 are dropped, never wrapped */
    c.mode = CH_FIXED; c.type = CT_MAJ7; c.bass = 1; uint8_t o[CH_MAXN];
    CHECK(ch_notes(&c, 125, o) == 2 && o[0] == 113 && o[1] == 125, "top of the range: notes above 127 dropped (%d notes)", ch_notes(&c, 125, o));
    CHECK(ch_notes(&c, 5, o) == 4, "bottom of the range: the bass below 0 is dropped");
    c.bass = 0;
    /* held keys: shared notes sound until both keys are up */
    ch_held h; memset(&h, 0, sizeof h);
    c.type = CT_MAJ;
    uint8_t a[CH_MAXN], b[CH_MAXN];
    int na = ch_notes(&c, 60, a), nb = ch_notes(&c, 64, b);              /* C E G and E G# B: E shared */
    log_[0] = 0; ch_down(&h, 7, a, na, 100, out, 0); ch_down(&h, 11, b, nb, 100, out, 0);
    CHECK(!strcmp(log_, "+60 +64 +67 +68 +71 "), "two chords on: %s", log_);
    log_[0] = 0; ch_up(&h, 7, out, 0);
    CHECK(!strcmp(log_, "-60 -67 "), "first up keeps the shared E: %s", log_);
    log_[0] = 0; ch_up(&h, 11, out, 0);
    CHECK(!strcmp(log_, "-64 -68 -71 "), "second up: %s", log_);
    log_[0] = 0; ch_up(&h, 11, out, 0); CHECK(!log_[0], "up of a released key does nothing");
    log_[0] = 0; ch_down(&h, 3, a, na, 90, out, 0); ch_down(&h, 3, a, na, 90, out, 0);
    CHECK(!strcmp(log_, "+60 +64 +67 -60 -64 -67 +60 +64 +67 "), "second down of a held key: %s", log_);
    log_[0] = 0; ch_all_off(&h, out, 0); CHECK(!strcmp(log_, "-60 -64 -67 "), "all off: %s", log_);
    for (int i = 0; i < 128; i++) CHECK(!h.count[i], "count %d left", i);
    /* drum mode: key 0 (C, midi 53 at octave 0) + major chord -> pads 0, 4, 7; folding at the top */
    uint8_t p[CH_MAXN];
    int np = ch_notes(&c, 53, a);
    CHECK(ch_drum_fold(0, 53, a, np, p) == 3 && p[0] == 0 && p[1] == 4 && p[2] == 7, "drum pads");
    c.type = CT_OCT; np = ch_notes(&c, 53 + 20, a);
    CHECK(ch_drum_fold(20, 53 + 20, a, np, p) == 1 && p[0] == 20, "drum: the octave above key 20 folds back onto it (one pad)");
    printf(fails ? "%d FAILED\n" : "chord ok\n", fails);
    return fails != 0;
}
