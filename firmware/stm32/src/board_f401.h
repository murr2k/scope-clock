/* board_f401.h -- Nucleo-F401RE: PWM "DAC" X-Y output (intermediate build).
 *
 * The STM32F401 has NO DAC peripheral (RM0368 documents TIM1, TIM2-5, TIM9-11
 * and no DAC/TIM6/TIM7 at all), so the deflection voltages are made with two
 * PWM channels on ONE timer plus an external 2-pole RC low-pass each:
 *
 *   PA6  TIM3_CH1 -> [1k] -+- [4k7] -+-> X  (scope CH1)
 *                          |         |
 *                        3n9       820p
 *                          |         |
 *                         GND       GND
 *   PA7  TIM3_CH2 -> same network -> Y  (scope CH2)
 *
 *   PA2/PA3  USART2 -> ST-Link virtual COM port (set time / mode from a
 *                      terminal; the ESP-01 line protocol works unchanged)
 *   PC13     USER button B1 (active LOW on Nucleo-64)
 *
 * Both channels live on TIM3 so they share a carrier and update coherently.
 * TIM2 is the sample clock; its update event drives two DMA streams (RM0368
 * Table 28: TIM2_UP = DMA1 Stream1 Ch3 and Stream7 Ch3), one per CCR, so X and
 * Y advance on the same edge.
 *
 * No Z-blanking in this build (it needs a transistor into the scope's Z input);
 * retrace is minimized by stroke ordering instead.
 */
#pragma once
#include <stdint.h>
#include <math.h>
#include <libopencm3/stm32/gpio.h>

typedef uint16_t ve_word_t;         /* CCR values fit in 16 bits */

#define BOARD_NAME          "nucleo-f401re"
#define BOARD_HAS_BUTTON    1
#define BOARD_HAS_ZBLANK    0

/* ---- PWM carrier ----
 * TIM3 ARR = PWM_ARR, so a period is (PWM_ARR+1) ticks and CCR spans
 * 0..PWM_TOP inclusive (257 levels, i.e. 8-bit plus the endpoint).
 */
#define TIMER_CLK_HZ        84000000UL
#define PWM_ARR             255u
#define PWM_TOP             256u
#define PWM_CARRIER_HZ      (TIMER_CLK_HZ / (PWM_ARR + 1u))   /* 328.125 kHz */

/* ---- sample clock ----
 * Locked to an integer division of the carrier (3 PWM periods per sample) so
 * the CCR updates never beat against the carrier.  109.375 kSa/s.
 */
#define SAMPLE_DIV          3u
#define SAMPLE_TIM_ARR            ((SAMPLE_DIV * (PWM_ARR + 1u)) - 1u)   /* 767 */
#define SAMPLE_RATE_HZ      (TIMER_CLK_HZ / (SAMPLE_DIV * (PWM_ARR + 1u)))

/* ---- frame budget ----
 * 2560 pts x 2 arrays x 2 bytes x 2 frames = 20.5 KB of the F401's 96 KB SRAM.
 * Floor refresh = SAMPLE_RATE_HZ / VE_MAX_POINTS ~= 43 Hz; real scenes land
 * near 400-2000 pts, i.e. 55 Hz (analog face) to 250 Hz (digital).
 */
#define VE_MAX_POINTS       2560u

/* Coarser default stroke step than the 12-bit board: there is no point
 * emitting samples finer than one PWM level (1/256 of full scale). */
#define VE_DEFAULT_STEP     0.015f

/* Blanked samples held at a stroke's start so the RC filter settles before the
 * beam lights.  4 samples = ~37 us ~= 8 dominant time constants of the stock
 * 1k/4n7 + 4k7/1n network.  Without this the beam paints a visible tail into
 * every stroke (an 'H' comes out looking like an 'A'). */
#define VE_DEFAULT_MOVE_SETTLE  4u

/* ---- user button (Nucleo B1, active low) ---- */
#define BUTTON_PORT         GPIOC
#define BUTTON_PIN          GPIO13
#define BUTTON_RCC          RCC_GPIOC
#define BUTTON_ACTIVE_HIGH  0
#define BUTTON_PUPD         GPIO_PUPD_PULLUP

/* ---- ESP-01 / VCP link ---- */
#define ESP_USART           USART2
#define ESP_BAUD            115200

/* world [-1,+1] -> CCR [0, PWM_TOP], clamped. */
static inline ve_word_t world_to_ccr(float w)
{
    int c = (int)lrintf((w * 0.5f + 0.5f) * (float)PWM_TOP);
    if (c < 0) c = 0;
    if (c > (int)PWM_TOP) c = PWM_TOP;
    return (ve_word_t)c;
}

/* Store one beam sample. Stream A feeds TIM3_CCR1 (X), stream B TIM3_CCR2 (Y).
 * `zon` is ignored: no Z-blank hardware on this board. */
static inline void frame_pack(ve_word_t *a, ve_word_t *b, uint16_t i,
                              float x, float y, int zon)
{
    (void)zon;
    a[i] = world_to_ccr(x);
    b[i] = world_to_ccr(y);
}

/* Roomy enough for all 60 minute ticks on the analog face. */
#define SCENE_MINUTE_TICKS  1
