/* board_g431.h -- NUCLEO-G431KB: the minimal-chip-count build.
 *
 * STM32G431KB: Cortex-M4F at 170 MHz, 128 KB flash, 32 KB SRAM, DAC1 with two
 * 12-bit channels on PA4/PA5, CORDIC, DMAMUX, TIM6/TIM7.
 *
 *   PA4  DAC1_OUT1  -> X deflection  (DHR12RD bits 11:0)
 *   PA5  DAC1_OUT2  -> Y deflection  (DHR12RD bits 27:16)
 *   PB6  Z-BLANK    -> scope Z / intensity via Q1
 *   PA2  USART2_TX  -> ST-Link virtual COM port (AF7)
 *   PA3  USART2_RX  <- ST-Link virtual COM port (AF7)
 *
 * Why this board wins on chip count: DAC1's channels have **built-in
 * rail-to-rail output buffers** that drive an external load directly, so the
 * dual op-amp the F407 build needs disappears.  That leaves one MCU + one
 * transistor for Z-blanking.  Adding an ESP-01 for SNTP/messages later still
 * only makes it two ICs.
 *
 * THE constraint on this part is SRAM: 32 KB total, and a double-buffered
 * frame costs 16 bytes per point (2 frames * 2 words * 4 bytes).  4096 points
 * would be 64 KB, i.e. twice the whole memory, so the budget drops to 1536
 * points (24 KB) and the analog face trades its 60 minute ticks for 12
 * five-minute ticks.  Refresh is unaffected -- at 125 kSa/s a 1536-point frame
 * still runs at 81 Hz, well clear of flicker.
 *
 * NUCLEO-G431KB has NO user button (Nucleo-32 has only RESET), so the display
 * mode is changed over the virtual COM port with `MODE=...`.
 */
#pragma once
#include <stdint.h>
#include <math.h>
#include <libopencm3/stm32/gpio.h>

typedef uint32_t ve_word_t;

#define BOARD_NAME          "nucleo-g431kb"
#define BOARD_HAS_ZBLANK    1
#define BOARD_HAS_BUTTON    0        /* Nucleo-32: RESET only */

/* ---- deflection DACs ---- */
#define DAC_MAX             4095u
#define X_IS_DAC1           1

/* ---- Z-axis blanking ---- */
#define ZBLANK_PORT         GPIOB
#define ZBLANK_PIN          GPIO6
#define ZBLANK_ACTIVE_HIGH  1

#if ZBLANK_ACTIVE_HIGH
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN)
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN << 16)
#else
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN << 16)
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN)
#endif

/* ---- ESP-01 / VCP link ---- */
#define ESP_USART           USART2
#define ESP_BAUD            115200

/* ---- sample clock / frame budget ----
 * 170 MHz / 1360 = 125.000 kSa/s exactly.
 */
#define TIMER_CLK_HZ        170000000UL
#define SAMPLE_RATE_HZ      125000u
#define VE_MAX_POINTS       1536u      /* 24 KB of the 32 KB SRAM */
#define VE_DEFAULT_STEP     0.013f
/* The buffered DAC settles in ~1 us against an 8 us sample period, so one
 * blanked sample of settling at a stroke start is ample. */
#define VE_DEFAULT_MOVE_SETTLE  1u
/* Drop the 48 in-between minute ticks: they cost ~380 points, which this
 * board's SRAM cannot spare.  Hour ticks and numerals stay. */
#define SCENE_MINUTE_TICKS  0

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
