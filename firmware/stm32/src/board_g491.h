/* board_g491.h -- NUCLEO-G491RE: the full-quality DAC build.
 *
 * STM32G491RE: Cortex-M4F at 170 MHz, 512 KB flash, 112 KB SRAM (96 KB
 * contiguous SRAM1+SRAM2 plus a 16 KB CCM), DAC1 with two buffered 12-bit
 * channels, CORDIC, DMAMUX, TIM6.
 *
 *   PA4  DAC1_OUT1  -> X deflection  (DHR12RD bits 11:0)
 *   PA5  DAC1_OUT2  -> Y deflection  (DHR12RD bits 27:16)   ** see SB6 below **
 *   PB6  Z-BLANK    -> scope Z / intensity via Q1
 *   PC13 USER B1    -> cycle display mode (active LOW, pulled up on the board)
 *   PA2  USART2_TX  -> ST-Link virtual COM port (AF7)
 *   PA3  USART2_RX  <- ST-Link virtual COM port (AF7)
 *
 * Same silicon family as the G431, so dac_dma_g4.c drives this unchanged:
 * identical DAC1 dual mode, identical DMAMUX request IDs, identical 170 MHz
 * clock tree.  What changes is memory, and it changes everything about the
 * scene budget.
 *
 * ** HARDWARE MOD: OPEN SOLDER BRIDGE SB6 **
 * On the MB1367 Nucleo-64, PA5 also drives the user LED LD2 (Arduino D13)
 * through SB6 and a transistor.  That is a load on the Y deflection output and
 * it is nonlinear, so leave SB6 fitted and the top of the picture skews.  SB6
 * exists precisely to disconnect it -- open it and PA5 is a clean DAC output.
 * You lose LD2 as an indicator, which this firmware does not use anyway.
 *
 * SRAM: 96 KB contiguous is plenty for the full 4096-point frame budget
 * (2 frames x 2 words x 4 bytes x 4096 = 64 KB), so this target needs NO
 * custom linker script -- contrast the G431, which had to claim its CCM alias
 * just to reach 32 KB.  The 16 KB CCM is left unused; it is spare headroom.
 */
#pragma once
#include <stdint.h>
#include <math.h>
#include <libopencm3/stm32/gpio.h>

typedef uint32_t ve_word_t;

#define BOARD_NAME          "nucleo-g491re"
#define BOARD_HAS_BUTTON    1
#define BOARD_HAS_ZBLANK    1

/* ---- deflection DACs ---- */
#define DAC_MAX             4095u
#define X_IS_DAC1           1

/* ---- Z-axis blanking ---- */
#define ZBLANK_PORT         GPIOB
#define ZBLANK_PIN          GPIO6
/* 0 = beam-ON drives the pin LOW.  Set for the Keysight InfiniiVision, whose
 * Z input is active-low for display: UG page 74, "When Z is low (<1.4 V), Y
 * versus X is displayed; when Z is high (>1.4 V), the trace is turned off."
 * An analog CRT with a bright-up Z input usually wants the opposite (1). */
#define ZBLANK_ACTIVE_HIGH  0

#if ZBLANK_ACTIVE_HIGH
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN)
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN << 16)
#else
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN << 16)
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN)
#endif

/* ---- user button: B1 on PC13, pulled up, shorts to ground ---- */
#define BUTTON_PORT         GPIOC
#define BUTTON_PIN          GPIO13
#define BUTTON_RCC          RCC_GPIOC
#define BUTTON_ACTIVE_HIGH  0

/* ---- ESP-01 / VCP link ---- */
#define ESP_USART           USART2
#define ESP_BAUD            115200

/* ---- sample clock / frame budget ----
 * 170 MHz / 850 = 200.000 kSa/s exactly, matching the F407 build.
 * 4096 points = 64 KB of the 96 KB contiguous SRAM -> >= 48.8 Hz floor.
 */
#define TIMER_CLK_HZ        170000000UL
#define SAMPLE_RATE_HZ      200000u
#define VE_MAX_POINTS       4096u
#define VE_DEFAULT_STEP     0.010f
/* The buffered DAC settles in ~1 us against a 5 us sample period. */
#define VE_DEFAULT_MOVE_SETTLE  1u
/* Room for the full face: all 60 minute ticks, unlike the SRAM-tight G431. */
#define SCENE_MINUTE_TICKS  1

static inline uint16_t world_to_dac(float w)
{
    int c = (int)lrintf((w * 0.5f + 0.5f) * (float)DAC_MAX);
    if (c < 0) c = 0;
    if (c > (int)DAC_MAX) c = DAC_MAX;
    return (uint16_t)c;
}

/* pack an (x,y) sample into a DHR12RD word: DACC2 (Y) high, DACC1 (X) low. */
static inline uint32_t pack_xy(float x, float y)
{
    return ((uint32_t)world_to_dac(y) << 16) | (uint32_t)world_to_dac(x);
}

static inline void frame_pack(ve_word_t *a, ve_word_t *b, uint16_t i,
                              float x, float y, int zon)
{
    a[i] = pack_xy(x, y);
    b[i] = zon ? ZBSRR_ON : ZBSRR_OFF;
}
