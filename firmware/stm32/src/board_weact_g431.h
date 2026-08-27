/* board_weact_g431.h -- WeAct Studio STM32G431CBU6 Core Board (V1.0, QFN48).
 *
 * Same silicon as the NUCLEO-G431KB target, so dac_dma_g4.c and the 32 KB
 * linker script apply unchanged.  What differs is the board around it, and two
 * of those differences are improvements over both Nucleos.
 *
 *   PA4  DAC1_OUT1  -> X deflection   (header P2, FREE - nothing else on it)
 *   PA5  DAC1_OUT2  -> Y deflection   (header P2, FREE - no LED, no SB6 mod)
 *   PB6  Z-BLANK    -> scope Z        (header P1; SB3/SB6 are unfitted)
 *   PC13 USER KEY   -> cycle mode     (active HIGH, needs internal pull-down)
 *   PC6  USER LED   -> spare indicator (active HIGH, blue)
 *
 * Verified against the maker's schematic, WeAct-STM32G431CxUxCoreBoard_V10
 * (docs/weact-g431cbu6-schematic.pdf).
 *
 * WHY THIS BOARD IS THE BEST HOST SO FAR
 *  1. **PA4/PA5 carry nothing else.**  The Nucleo-64 hangs LD2 off PA5 and
 *     needs SB6 lifted before the Y axis is trustworthy; here the DAC pins go
 *     straight to the header.
 *  2. **A real 8 MHz HSE crystal (X2, XTAL3225, +/-10 ppm).**  Since the
 *     timebase is SysTick (see rtc.c), the system clock *is* the clock's
 *     accuracy: HSI is ~1% (~14 min/day), this crystal is ~10 ppm (~1 s/day).
 *     That is a three-order-of-magnitude improvement for free.
 *
 * WHAT IT COSTS
 *  - 32 KB SRAM again (22 KB contiguous + a 10 KB CCM alias), so the frame
 *    budget is 1536 points and the analog face drops to 12 five-minute ticks,
 *    exactly as on the G431KB.  The G491's full 4096-point face does not fit.
 *  - No on-board debugger and no USB-serial bridge.  Flash over the P3 SWD
 *    header (PA13/PA14) with an external ST-Link, or via USB DFU (hold BOOT0 =
 *    SW3 on PB8).  With no VCP there is no T=/MODE= path unless a USB-TTL
 *    adapter is wired to a USART -- the PC13 KEY cycles modes instead.
 *
 * X1 (32.768 kHz LSE) and the VBAT/BAT54C circuit are populated, so a hardware
 * RTC is possible here.  Not used: libopencm3 has no G4 RTC support.
 */
#pragma once
#include <stdint.h>
#include <math.h>
#include <libopencm3/stm32/gpio.h>

typedef uint32_t ve_word_t;

#define BOARD_NAME          "weact-g431cbu6"
#define BOARD_HAS_BUTTON    1
#define BOARD_HAS_ZBLANK    1

/* ---- deflection DACs ---- */
#define DAC_MAX             4095u
#define X_IS_DAC1           1

/* ---- Z-axis blanking ---- */
#define ZBLANK_PORT         GPIOB
#define ZBLANK_PIN          GPIO6
/* 0 = beam-ON drives the pin LOW, matching a Keysight InfiniiVision
 * (UG p74: Z low displays, Z high blanks).  Set 1 for a CRT that brightens
 * on a positive Z. */
#define ZBLANK_ACTIVE_HIGH  0

#if ZBLANK_ACTIVE_HIGH
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN)
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN << 16)
#else
#  define ZBSRR_ON   ((uint32_t)ZBLANK_PIN << 16)
#  define ZBSRR_OFF  ((uint32_t)ZBLANK_PIN)
#endif

/* ---- user KEY: PC13 -- 330R -- SW2 -- 3V3.  Pressing pulls the pin HIGH and
 * there is NO external pull-down, so the internal one is required. ---- */
#define BUTTON_PORT         GPIOC
#define BUTTON_PIN          GPIO13
#define BUTTON_RCC          RCC_GPIOC
#define BUTTON_ACTIVE_HIGH  1
#define BUTTON_PUPD         GPIO_PUPD_PULLDOWN

/* ---- command UART: no VCP on this board.  These pins are on header P1 for a
 * USB-TTL adapter (adapter TX -> PA10; the firmware never transmits). ---- */
#define ESP_USART           USART1
#define ESP_BAUD            115200

/* ---- sample clock / frame budget: identical to the G431KB ---- */
#define TIMER_CLK_HZ        170000000UL
#define SAMPLE_RATE_HZ      125000u
#define VE_MAX_POINTS       1536u      /* 24 KB of the 32 KB SRAM */
#define VE_DEFAULT_STEP     0.013f
#define VE_DEFAULT_MOVE_SETTLE  1u
#define SCENE_MINUTE_TICKS  0

static inline uint16_t world_to_dac(float w)
{
    int c = (int)lrintf((w * 0.5f + 0.5f) * (float)DAC_MAX);
    if (c < 0) c = 0;
    if (c > (int)DAC_MAX) c = DAC_MAX;
    return (uint16_t)c;
}

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
