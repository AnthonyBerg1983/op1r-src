/* Oblique Strategies card (docs/23), shown in the clock slot: Shift+help toggles card / clock (dispatcher.c).
 * Idea and card list from https://github.com/ultralazr/op1-oblique-strategies (MIT; card texts by Brian Eno and
 * Peter Schmidt). Text in the firmware font (Text objects, centred by the firmware). Glyphs the font lacks
 * (' ( ) [ ]) are set as spaces and drawn by us at the x the Text layout gives that character (+0x84 array). */
#include "../op1_synth.h"

extern const int ob_ncards;
extern const uint16_t ob_first[];
extern const char *const ob_lines[];

#define OB_MAXL     6
#define OB_LEN      64
#define OB_SIZE     12                       /* Text size: as the monitor rows */
#define OB_PITCH    18                       /* px between lines */
#define OB_CAP      9                        /* cap height in px at OB_SIZE (glyph strokes) */
#define OB_GDY      0                        /* glyph top relative to the Text y */
#define OB_CX       160
#define OB_MID_Y    86                       /* vertical centre of the body block (below the title) */

/* Text object fields (Text.cpp, docs/23): +0x58 x / +0x5C y (stored minus a bias, scaled on some display
 * variants), +0x60 alignment (1 = centred on x; ctor arg 6), +0x84..+0x88 per-char x in px (from op1_text_set) */
#ifdef OP1_HOST                                             /* host mock: 64-bit pointers, own layout */
int32_t *host_text_pos(void *t, int32_t *n);
void host_text_align(void *t, int a);
#define T_SET_ALIGN(t, a) host_text_align((t), (a))
#else
#define T_SET_ALIGN(t, a) (*(int32_t *)((uint8_t *)(t) + 0x60) = (a))
static int32_t *host_text_pos(void *t, int32_t *n)
{
    int32_t *p0 = *(int32_t **)((uint8_t *)t + 0x84), *p1 = *(int32_t **)((uint8_t *)t + 0x88);
    *n = (int32_t)(p1 - p0);
    return p0;
}
#endif

/* one Text per line slot for each line count (y goes through the ctor, which handles the display variants) */
#define OB_SLOTS    (OB_MAXL * (OB_MAXL + 1) / 2)
#define OB_SLOT(nl, i) (((nl) - 1) * (nl) / 2 + (i))
static uint32_t ob_txt[OB_SLOTS + 1][OP1_TEXT_SIZE / 4];            /* line slots, title */
#define OB_TITLE    OB_SLOTS
static char ob_buf[OB_MAXL][OB_LEN];
static int ob_nl, ob_card = -1;
static int ob_line_y(int nl, int i) { return OB_MID_Y - ((nl - 1) * OB_PITCH + OB_CAP) / 2 + i * OB_PITCH; }
static uint32_t ob_rng;
static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
static const uint8_t c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF },
                     c_grey[4] = { 0x6F, 0x71, 0x73, 0xFF };

static int ob_extra(char c) { return c == '\'' || c == '(' || c == ')' || c == '[' || c == ']'; }

static uint32_t ob_rand(void)
{
    uint32_t c;
#ifdef OP1_HOST
    static uint32_t k; c = k += 0x9E3779B9u;
#else
    __asm__ volatile ("%0 = CYCLES;" : "=d"(c));
#endif
    ob_rng = ob_rng * 1664525u + 1013904223u + c;
    return ob_rng >> 8;
}

static void ob_set(int card)
{
    ob_card = card;
    ob_nl = ob_first[card + 1] - ob_first[card];
    if (ob_nl > OB_MAXL) ob_nl = OB_MAXL;
    for (int i = 0; i < ob_nl; i++) {
        const char *s = ob_lines[ob_first[card] + i];
        int k = 0;
        for (; s[k] && k < OB_LEN - 1; k++) ob_buf[i][k] = ob_extra(s[k]) ? ' ' : s[k];
        ob_buf[i][k] = 0;
        op1_text_set(ob_txt[OB_SLOT(ob_nl, i)], ob_buf[i]);
    }
}

/* new random card, never the same one twice in a row (GUI thread: op1_text_set reallocates the Text vectors) */
static void ob_pick(void)
{
    int n = ob_ncards, c;
    do c = (int)(((ob_rand() & 0xFFFF) * (uint32_t)n) >> 16); while (n > 1 && c == ob_card);
    ob_set(c);
}

static void ob_init(void *scr)
{
    (void)scr;
    for (int nl = 1; nl <= OB_MAXL; nl++)
        for (int i = 0; i < nl; i++) {
            op1_text_ctor(ob_txt[OB_SLOT(nl, i)], OP1_Q10(OB_CX), ob_line_y(nl, i), OB_SIZE, c_white);
            T_SET_ALIGN(ob_txt[OB_SLOT(nl, i)], 1);
        }
    op1_text_ctor(ob_txt[OB_TITLE], OP1_Q10(OB_CX), 12, OB_SIZE, c_red);
    T_SET_ALIGN(ob_txt[OB_TITLE], 1);
    op1_text_set(ob_txt[OB_TITLE], "oblique strategies");
    ob_pick();
}
static volatile uint8_t ob_want;
void ob_new_card(void) { ob_want = 1; }                     /* key thread: picked at the next draw */

static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, w, c);
}

/* glyph at (x, y) px top-left, OB_CAP high, 1.5 px strokes like the font */
static void ob_glyph(void *r, char c, int x, int y)
{
    const int32_t w = 0x600, X = OP1_Q10(x), Y = OP1_Q10(y), H = OP1_Q10(OB_CAP), u = 1024;
    switch (c) {
    case '\'': ln(r, X + 2 * u, Y, X + 2 * u, Y + 3 * u, w, c_white); break;
    case '(':  ln(r, X + 4 * u, Y, X + 2 * u, Y + 2 * u, w, c_white); ln(r, X + 2 * u, Y + 2 * u, X + 2 * u, Y + H - 2 * u, w, c_white);
               ln(r, X + 2 * u, Y + H - 2 * u, X + 4 * u, Y + H, w, c_white); break;
    case ')':  ln(r, X + 1 * u, Y, X + 3 * u, Y + 2 * u, w, c_white); ln(r, X + 3 * u, Y + 2 * u, X + 3 * u, Y + H - 2 * u, w, c_white);
               ln(r, X + 3 * u, Y + H - 2 * u, X + 1 * u, Y + H, w, c_white); break;
    case '[':  ln(r, X + 4 * u, Y, X + 2 * u, Y, w, c_white); ln(r, X + 2 * u, Y, X + 2 * u, Y + H, w, c_white);
               ln(r, X + 2 * u, Y + H, X + 4 * u, Y + H, w, c_white); break;
    case ']':  ln(r, X + 1 * u, Y, X + 3 * u, Y, w, c_white); ln(r, X + 3 * u, Y, X + 3 * u, Y + H, w, c_white);
               ln(r, X + 3 * u, Y + H, X + 1 * u, Y + H, w, c_white); break;
    }
}

static void ob_update(void *scr, int dt)
{
    if (ob_want) { ob_want = 0; ob_pick(); }
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    ln(r, OP1_Q10(24), OP1_Q10(30), OP1_Q10(296), OP1_Q10(30), 0x400, c_grey);   /* under the title */
    op1_text_draw(ob_txt[OB_TITLE], r, ident);
    if (ob_card < 0) return;
    for (int i = 0; i < ob_nl; i++) {
        void *t = ob_txt[OB_SLOT(ob_nl, i)];
        op1_text_draw(t, r, ident);
        const char *s = ob_lines[ob_first[ob_card] + i];
        int32_t n, *p = host_text_pos(t, &n);
        for (int k = 0; s[k] && k < n; k++)
            if (ob_extra(s[k])) ob_glyph(r, s[k], p[k], ob_line_y(ob_nl, i) + OB_GDY);
    }
}

const op1_screen_t SCREEN_oblique = { ob_init, ob_update };
