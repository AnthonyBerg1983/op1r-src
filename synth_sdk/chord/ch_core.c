/* Chord mode core (ch_core.h). */
#include "ch_core.h"

static const int8_t fixed_iv[CT_N][5] = {          /* intervals from the root, -1 = end */
    { 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 2, 7, -1 }, { 0, 5, 7, -1 }, { 0, 4, 7, 10, -1 }, { 0, 4, 7, 11, -1 },
    { 0, 3, 7, 10, -1 }, { 0, 3, 6, -1 }, { 0, 4, 8, -1 }, { 0, 4, 7, 9, -1 }, { 0, 4, 7, 14, -1 }, { 0, 7, -1 },
    { 0, 12, -1 } };
static const uint8_t scales[SC_N][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                         { 0, 2, 4, 5, 7, 9, 10 } };
static const int8_t scale_deg[SK_N][5] = {          /* scale degrees above the played degree, -1 = end */
    { 0, 2, 4, -1 }, { 0, 2, 4, 6, -1 }, { 0, 1, 4, -1 }, { 0, 3, 4, -1 }, { 0, 2, 4, 8, -1 } };

static void sort(uint8_t *a, int n)
{
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && a[j - 1] > a[j]; j--) { uint8_t t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
}

/* the chord before voicing: root note (MIDI) and intervals; scale mode snaps a note outside the scale down */
static int raw_chord(const ch_cfg *c, int midi, int *root, int8_t *iv)
{
    int n = 0;
    if (c->mode == CH_FIXED) {
        const int8_t *t = fixed_iv[c->type < CT_N ? c->type : 0];
        while (n < 5 && t[n] >= 0) { iv[n] = t[n]; n++; }
        *root = midi;
        return n;
    }
    const uint8_t *sc = scales[c->scale < SC_N ? c->scale : 0];
    int rel = ((midi - c->root) % 12 + 12) % 12, deg = 0;
    for (int d = 0; d < 7; d++) if (sc[d] <= rel) deg = d;      /* the scale note at or below */
    *root = midi - (rel - sc[deg]);
    const int8_t *t = scale_deg[c->type < SK_N ? c->type : 0];
    while (n < 5 && t[n] >= 0) {
        int d = deg + t[n];
        iv[n] = (int8_t)(sc[d % 7] + 12 * (d / 7) - sc[deg]);
        n++;
    }
    return n;
}

int ch_notes(const ch_cfg *c, int midi, uint8_t *out)
{
    int root, nn[CH_MAXN];
    int8_t iv[5];
    int n = raw_chord(c, midi, &root, iv);
    for (int i = 0; i < n; i++) nn[i] = root + iv[i];
    switch (c->voicing) {                                     /* nn ascending before and after */
    case VO_INV1: case VO_INV2: case VO_INV3: {
        int k = c->voicing - VO_INV1 + 1;
        for (int j = 0; j < k && n > 1; j++) {               /* lowest note up an octave, k times */
            int t = nn[0] + 12;
            for (int i = 0; i < n - 1; i++) nn[i] = nn[i + 1];
            nn[n - 1] = t;
        }
        break;
    }
    case VO_DROP2:                                           /* second highest down an octave */
        if (n >= 3) { int t = nn[n - 2] - 12; for (int i = n - 2; i > 0; i--) nn[i] = nn[i - 1]; nn[0] = t; }
        break;
    case VO_SPREAD:                                          /* root down, third up an octave */
        if (n >= 3) { nn[0] -= 12; nn[1] += 12; }
        break;
    }
    if (c->bass && n < CH_MAXN) nn[n++] = root - 12;
    int m = 0;
    uint8_t o[CH_MAXN];
    for (int i = 0; i < n; i++) if (nn[i] >= 0 && nn[i] <= 127) o[m++] = (uint8_t)nn[i];
    sort(o, m);
    int k = 0;
    for (int i = 0; i < m; i++) if (!k || out[k - 1] != o[i]) out[k++] = o[i];   /* no doubled notes */
    return k;
}

static const char *const note_nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

void ch_name(const ch_cfg *c, int midi, char *s)
{
    int root;
    int8_t iv[5];
    int n = raw_chord(c, midi, &root, iv);
    int mask = 0;
    for (int i = 0; i < n; i++) mask |= 1 << (iv[i] % 12);
    const char *q = "";
    switch (mask) {                                          /* quality from the pitch classes */
    case 0x091: q = ""; break;         case 0x089: q = "min"; break;      case 0x049: q = "dim"; break;
    case 0x111: q = "aug"; break;      case 0x491: q = "7"; break;      case 0x891: q = "maj7"; break;
    case 0x489: q = "min7"; break;     case 0x449: q = "min7b5"; break;   case 0x085: q = "sus2"; break;
    case 0x0A1: q = "sus4"; break;     case 0x291: q = "6"; break;      case 0x095: q = "add9"; break;
    case 0x08D: q = "minadd9"; break;    case 0x081: q = "5"; break;      case 0x001: q = "oct"; break;
    case 0x4A1: q = "7sus4"; break;    case 0x083: q = "susb2"; break;  case 0x0C1: q = "sus#4"; break;
    case 0x043: q = "susb2b5"; break;  case 0x061: q = "sus4b5"; break; case 0x08B: q = "minaddb9"; break;
    case 0x04B: q = "dimaddb9"; break; default: q = "?"; break;     /* (scale mode: sus / add9 on odd degrees) */
    }
    char *p = put(put(s, note_nm[(root % 12 + 12) % 12]), q);
    uint8_t o[CH_MAXN];
    int m = ch_notes(c, midi, o);
    if (m && (o[0] % 12) != (root % 12 + 12) % 12) { *p++ = '/'; put(p, note_nm[o[0] % 12]); }   /* inversion: bass */
}

void ch_up(ch_held *h, int key, ch_out_fn out, void *ctx)
{
    if (key < 0 || key >= CH_KEYS) return;
    for (int i = 0; i < h->n[key]; i++) {
        int note = h->notes[key][i];
        if (h->count[note] && --h->count[note] == 0) out(note, 0, 0, ctx);
    }
    h->n[key] = 0;
}

void ch_down(ch_held *h, int key, const uint8_t *notes, int n, int vel, ch_out_fn out, void *ctx)
{
    if (key < 0 || key >= CH_KEYS) return;
    if (h->n[key]) ch_up(h, key, out, ctx);
    if (n > CH_MAXN) n = CH_MAXN;
    for (int i = 0; i < n; i++) {
        int note = notes[i] & 127;
        h->notes[key][i] = (uint8_t)note;
        if (h->count[note]++ == 0) out(note, vel, 1, ctx);
    }
    h->n[key] = (uint8_t)n;
}

int ch_add(ch_held *h, int key, int note, int vel, ch_out_fn out, void *ctx)
{
    if (key < 0 || key >= CH_KEYS || h->n[key] >= CH_MAXN) return 0;
    note &= 127;
    h->notes[key][h->n[key]++] = (uint8_t)note;
    if (h->count[note]++ == 0) out(note, vel, 1, ctx);
    return 1;
}

void ch_all_off(ch_held *h, ch_out_fn out, void *ctx)
{
    for (int k = 0; k < CH_KEYS; k++) ch_up(h, k, out, ctx);
}

int ch_drum_fold(int key, int midi, const uint8_t *notes, int n, uint8_t *pads)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        int p = key + (notes[i] - midi);
        while (p < 0) p += 12;
        while (p >= CH_KEYS) p -= 12;
        int dup = 0;
        for (int j = 0; j < m; j++) dup |= pads[j] == p;
        if (!dup) pads[m++] = (uint8_t)p;
    }
    return m;
}
