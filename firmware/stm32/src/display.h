/* display.h -- the streamed-frame engine, board-independent interface.
 *
 * A sample-clock timer paces two circular DMA streams that together emit one
 * beam sample per tick.  The active frame loops forever (a static image, zero
 * CPU); display_present() queues a freshly built frame and the swap happens at
 * the next frame boundary in the transfer-complete ISR, so nothing tears.
 *
 * Backends (one is compiled per env, see platformio.ini build_src_filter):
 *   dac_dma.c  F407: TIM6 -> dual 12-bit DAC (DHR12RD) + GPIO BSRR for Z
 *   pwm_dma.c  F401: TIM2 -> TIM3 CCR1/CCR2 PWM pair, external RC filters
 */
#pragma once
#include "vector_engine.h"

void display_init(frame_t *initial);   /* configure HW, start streaming `initial` */
void display_present(frame_t *f);      /* queue `f`; swapped in at next boundary   */
int  display_swap_pending(void);       /* nonzero while a queued swap is unconsumed */

#if BOARD_HAS_ZBLANK
void z_blank_now(void);                /* force the beam blank immediately */
#else
static inline void z_blank_now(void) { }
#endif
