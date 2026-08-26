/* vector_engine.c -- see vector_engine.h.  Reference: tools/vengine.py */
#include <math.h>
#include "vector_engine.h"
#include "font_vec.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

ve_cfg_t ve_cfg = { .step = VE_DEFAULT_STEP, .move_step = 0.05f,
                    .move_min = 3, .move_max = 14,
                    .move_settle = VE_DEFAULT_MOVE_SETTLE };

static frame_t *g;          /* frame under construction */
static float    cx, cy;     /* current beam position (world) */
static int      pen_down;
static int      started;

static inline void emit(float x, float y, int zon)
{
    if (g->n >= VE_MAX_POINTS)      /* budget guard: drop the overflow */
        return;
    frame_pack(g->stream_a, g->stream_b, g->n, x, y, zon);
    g->n++;
}

void ve_begin(frame_t *f)
{
    g = f;
    g->n = 0;
    cx = cy = 0.0f;
    pen_down = 0;
    started = 0;
}

void ve_move(float x, float y)
{
    float dx = x - cx, dy = y - cy;
    float dist = sqrtf(dx * dx + dy * dy);
    int n = (int)ceilf(dist / ve_cfg.move_step);
    if (n < ve_cfg.move_min) n = ve_cfg.move_min;
    if (n > ve_cfg.move_max) n = ve_cfg.move_max;
    for (int k = 1; k <= n; k++) {
        float t = (float)k / (float)n;
        emit(cx + dx * t, cy + dy * t, 0);
    }
    for (uint8_t s = 0; s < ve_cfg.move_settle; s++)
        emit(x, y, 0);                  /* let the output stage catch up */
    cx = x; cy = y;
    pen_down = 0;
    started = 1;
}

void ve_line(float x, float y)
{
    if (!started) { emit(cx, cy, 0); started = 1; }   /* blank the DMA wrap */
    if (!pen_down) { emit(cx, cy, 1); pen_down = 1; }  /* light start vertex */
    float dx = x - cx, dy = y - cy;
    float dist = sqrtf(dx * dx + dy * dy);
    int n = (int)ceilf(dist / ve_cfg.step);
    if (n < 1) n = 1;
    for (int k = 1; k <= n; k++) {
        float t = (float)k / (float)n;
        emit(cx + dx * t, cy + dy * t, 1);
    }
    cx = x; cy = y;
}

void ve_dwell(uint16_t count)
{
    for (uint16_t i = 0; i < count; i++)
        emit(cx, cy, 1);
}

void ve_poly(const float *pts, uint16_t npairs, int closed)
{
    if (npairs == 0) return;
    ve_move(pts[0], pts[1]);
    for (uint16_t i = 1; i < npairs; i++)
        ve_line(pts[2 * i], pts[2 * i + 1]);
    if (closed)
        ve_line(pts[0], pts[1]);
}

void ve_arc(float acx, float acy, float rx, float ry,
            float a0deg, float a1deg, uint16_t seg)
{
    if (seg < 1) seg = 1;
    for (uint16_t i = 0; i <= seg; i++) {
        float a = (a0deg + (a1deg - a0deg) * (float)i / (float)seg)
                  * (float)M_PI / 180.0f;
        float x = acx + rx * cosf(a);
        float y = acy + ry * sinf(a);
        if (i == 0) ve_move(x, y);
        else        ve_line(x, y);
    }
}

void ve_circle(float acx, float acy, float r, uint16_t seg)
{
    ve_arc(acx, acy, r, r, 0.0f, 360.0f, seg);
}

float ve_text_width(const char *s, float scale, float tracking)
{
    float w = 0.0f;
    for (const char *p = s; *p; p++) {
        int code = (unsigned char)*p;
        if (code < FONT_FIRST || code > FONT_LAST) code = ' ';
        w += (float)font_glyphs[code - FONT_FIRST].advance * scale + tracking;
    }
    return w;
}

float ve_text(float x, float y, const char *s, float scale, float tracking)
{
    float penx = x;
    for (const char *p = s; *p; p++) {
        int code = (unsigned char)*p;
        if (code < FONT_FIRST || code > FONT_LAST) code = ' ';
        const glyph_t *gl = &font_glyphs[code - FONT_FIRST];
        for (uint8_t sidx = 0; sidx < gl->n_stroke; sidx++) {
            const fstroke_t *st = &font_strokes[gl->first_stroke + sidx];
            for (uint8_t k = 0; k < st->n; k++) {
                const fpt_t *pt = &font_pts[st->first + k];
                float wx = penx + ((float)pt->x / (float)FONT_FP) * scale;
                float wy = y    + ((float)pt->y / (float)FONT_FP) * scale;
                if (k == 0) ve_move(wx, wy);
                else        ve_line(wx, wy);
            }
        }
        penx += (float)gl->advance * scale + tracking;
    }
    return penx;
}

uint16_t ve_end(void)
{
    return g->n;
}
