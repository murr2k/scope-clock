/* board_f407.h -- STM32F407VG (F4-Discovery): true dual 12-bit DAC output.
 *
 *   PA4  DAC1_OUT1  -> X deflection (via op-amp buffer)   [DHR12RD bits 11:0]
 *   PA5  DAC1_OUT2  -> Y deflection (via op-amp buffer)   [DHR12RD bits 27:16]
 *   PE2  Z-BLANK    -> scope Z / intensity (via Q1)       [GPIO BSRR by DMA]
 *   PA2  USART2_TX  -> ESP-01 RX
 *   PA3  USART2_RX  <- ESP-01 TX
 *   PA0  USER BTN   -> cycle display mode (active high on Discovery)
 *   PC14/PC15 OSC32 -> 32.768 kHz LSE for the RTC (see caveat below)
 *
 * LSE caveat: the stock F4-Discovery does NOT populate the 32.768 kHz crystal
 * (footprint X2 is empty).  Populate it for an accurate RTC, or build with
 * -DUSE_LSI to fall back to the internal ~32 kHz RC (drifty; lean on SNTP).
 */
#pragma once
#include <stdint.h>
#include <math.h>
#include <libopencm3/stm32/gpio.h>

typedef uint32_t ve_word_t;

#define BOARD_NAME         "disco-f407vg"
#define BOARD_HAS_BUTTON    1
#define BOARD_HAS_ZBLANK   1

/* ---- deflection DACs ---- */
#define DAC_MAX            4095u
#define X_IS_DAC1          1        /* PA4 = channel 1 = low 12 bits of DHR12RD */

/* ---- Z-axis blanking ---- */
#define ZBLANK_PORT        GPIOE
#define ZBLANK_PIN         GPIO2
#define ZBLANK_ACTIVE_HIGH 1        /* 1: beam-ON drives the pin HIGH */

/* BSRR words the Z-DMA streams onto GPIOE_BSRR, one per beam sample. */
#if ZBLANK_ACTIVE_HIGH
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN)            /* set   -> HIGH */
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN << 16)      /* reset -> LOW  */
#else
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN << 16)
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN)
#endif

/* ---- user button (Discovery B1, active high) ---- */
#define BUTTON_PORT        GPIOA
#define BUTTON_PIN         GPIO0
#define BUTTON_RCC         RCC_GPIOA
#define BUTTON_ACTIVE_HIGH 1
#define BUTTON_PUPD        GPIO_PUPD_NONE   /* Discovery has an external pull-down */

/* ---- ESP-01 link ---- */
#define ESP_USART          USART2
#define ESP_BAUD           115200

/* ---- sample clock / frame budget ---- */
#define TIMER_CLK_HZ       84000000UL  /* APB1 timer clock at 168 MHz sysclk */
#define SAMPLE_RATE_HZ     200000u     /* TIM6 update rate */
#define VE_MAX_POINTS      4096u       /* one DMA frame (>= 48.8 Hz refresh) */
#define VE_DEFAULT_STEP    0.010f
/* The DAC settles well inside one sample period, so no extra settling. */
#define VE_DEFAULT_MOVE_SETTLE  0u

/* world [-1,+1] -> DAC count [0,DAC_MAX], clamped. */
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

/* Store one beam sample. Stream A feeds DAC_DHR12RD, stream B the Z GPIO. */
static inline void frame_pack(ve_word_t *a, ve_word_t *b, uint16_t i,
                              float x, float y, int zon)
{
    a[i] = pack_xy(x, y);
    b[i] = zon ? ZBSRR_ON : ZBSRR_OFF;
}

/* Roomy enough for all 60 minute ticks on the analog face. */
#define SCENE_MINUTE_TICKS  1
