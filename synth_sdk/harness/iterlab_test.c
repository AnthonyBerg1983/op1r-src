/* Host preview of the stock-iter lab screen (screens/iterlab.c): prints JSON frames for harness/render_iterlab.py */
#include "../screens/iterlab.c"
#include <math.h>
#include <stdio.h>
#include <string.h>

int16_t host_sine[8192]; int32_t host_pitch_inc[128]; uint8_t host_voice_mem[6 * 0x24];
int16_t host_knobs[8][8]; int host_slot; const uint8_t *host_sample_buffer[8];
int16_t host_fx_knobs[3][8]; int16_t host_fx_meter[3]; void *host_fx_scratch[3];
volatile uint8_t op1_fx_page[3][4];
volatile int16_t op1_iter_level[6];
int host_held_count, host_held_note;

#define MAXL 512
static char node_id[8][16]; static int32_t node_m[8][9]; static int nnodes;
static struct { int32_t x0, y0, x1, y1, w; uint8_t c[4]; } lines[MAXL]; static int nlines;
void *host_find_node(void *s, const char *id) { (void)s; snprintf(node_id[nnodes], 16, "%s", id); return (void *)(intptr_t)(++nnodes); }
void host_node_set_transform(void *n, const int32_t *m) { memcpy(node_m[(intptr_t)n - 1], m, 36); }
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; nlines = 0; }
void *host_raster(void) { return &nlines; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c)
{ (void)r; const op1_pt *p = a, *q = b; if (nlines < MAXL) { lines[nlines].x0 = p->x; lines[nlines].y0 = p->y; lines[nlines].x1 = q->x; lines[nlines].y1 = q->y; lines[nlines].w = w; memcpy(lines[nlines].c, c, 4); nlines++; } }

static void frame(const char *label)
{
    printf("{\"label\": \"%s\", \"nodes\": {", label);
    for (int i = 0; i < nnodes; i++) { printf("%s\"%s\": [", i ? ", " : "", node_id[i]); for (int j = 0; j < 9; j++) printf("%s%d", j ? ", " : "", node_m[i][j]); printf("]"); }
    printf("}, \"lines\": [");
    for (int i = 0; i < nlines; i++) printf("%s[%d, %d, %d, %d, %d, [%d, %d, %d, %d]]", i ? ", " : "", lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1, lines[i].w, lines[i].c[0], lines[i].c[1], lines[i].c[2], lines[i].c[3]);
    printf("]}\n");
}
static void state(const char *label, int k0, int k1, int k2, int k3, int voices, int level, int note, int frames)
{
    host_knobs[0][0] = (int16_t)k0; host_knobs[0][1] = (int16_t)k1; host_knobs[0][2] = (int16_t)k2; host_knobs[0][3] = (int16_t)k3;
    for (int i = 0; i < 6; i++) op1_iter_level[i] = (int16_t)(i < voices ? level - i * 2000 : 0);
    host_held_count = voices; host_held_note = note;
    for (int f = 0; f < frames; f++) lab_update(0, 16);
    frame(label);
}
int main(void)
{
    for (int i = 0; i < 8192; i++) host_sine[i] = (int16_t)lrint(32767.0 * sin(2 * M_PI * i / 8192.0));
    lab_init(0);
    state("idle, knobs at minimum", 0, 0, 0, 0, 0, 0, 0, 20);
    state("idle, knobs mid / quarter / max / three-quarter", 16384, 8192, 32767, 24576, 0, 0, 0, 20);
    state("one note (C4 = 60), medium level", 12000, 20000, 9000, 26000, 1, 12000, 60, 23);
    state("three voices, loud, note 72", 12000, 20000, 30000, 26000, 3, 30000, 72, 37);
    state("quiet note, blue max (fast bubbles), red max (dashes)", 32767, 8000, 4000, 32767, 1, 4000, 55, 29);
    return 0;
}
