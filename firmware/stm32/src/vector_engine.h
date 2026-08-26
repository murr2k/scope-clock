/* vector_engine.h -- display-list -> beam-sample frame builder.
 *
 * Mirrors tools/vengine.py (the host reference).  Scenes call ve_move/ve_line/
 * ve_text/... which emit packed DHR12RD words (xy[]) and GPIO BSRR words (z[])
 * straight into the frame the DMA will stream.  Fixed-step interpolation keeps
 * lit-sample spacing (== brightness) uniform; blanked moves cover pen-up jumps.
 */
#pragma once
#include <stdint.h>
#include "board.h"

/* A frame is two parallel word arrays, one per DMA stream.  What they mean is
 * board-specific (see frame_pack() in the board header):
 *   F407: stream_a -> DAC_DHR12RD (packed X|Y),  stream_b -> GPIO BSRR (Z)
 *   F401: stream_a -> TIM3_CCR1   (X),           stream_b -> TIM3_CCR2 (Y)
 */
typedef struct {
    ve_word_t stream_a[VE_MAX_POINTS];
    ve_word_t stream_b[VE_MAX_POINTS];
    uint16_t  n;
} frame_t;

typedef struct {
    float   step;        /* world units between lit samples (brightness)  */
    float   move_step;   /* world units between blanked samples (retrace) */
    uint8_t move_min;
    uint8_t move_max;
    /* Blanked samples held AT the destination after a pen-up move.  A slow
     * output stage (the F401's RC-filtered PWM) is still in transit when it
     * arrives; lighting the beam too early paints a tail from the previous
     * stroke.  Zero on the F407, whose DAC settles within a sample. */
    uint8_t move_settle;
} ve_cfg_t;

extern ve_cfg_t ve_cfg;

void     ve_begin(frame_t *f);
void     ve_move(float x, float y);
void     ve_line(float x, float y);
void     ve_dwell(uint16_t count);
void     ve_poly(const float *pts, uint16_t npairs, int closed);
void     ve_arc(float cx, float cy, float rx, float ry,
                float a0deg, float a1deg, uint16_t seg);
void     ve_circle(float cx, float cy, float r, uint16_t seg);
float    ve_text(float x, float y, const char *s, float scale, float tracking);
float    ve_text_width(const char *s, float scale, float tracking);
uint16_t ve_end(void);
