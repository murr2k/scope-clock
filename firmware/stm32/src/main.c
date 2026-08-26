/* main.c -- scope clock top level.
 *
 * The heavy lifting runs in hardware: TIM6 + dual DMA stream the active frame
 * to the DACs and the Z GPIO forever.  The CPU only rebuilds a frame when the
 * second changes or the mode/message updates, then sleeps in WFI.
 */
#include <string.h>
#include <stdio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/scb.h>

#include "board.h"
#include "vector_engine.h"
#include "scenes.h"
#include "display.h"
#include "rtc.h"
#include "esplink.h"

static const char *const MONTHS[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

static frame_t frames[2];
static uint8_t  build_idx = 1;           /* frames[0] is the initial active */

static volatile scene_mode_t g_mode = MODE_ANALOG;
static char            g_message[64] = "SCOPE CLOCK";
static volatile int    g_dirty = 1;

/* ---- ESP-01 command handlers (declared in esplink.h) ---- */
void app_set_epoch(uint32_t utc)      { rtc_set_epoch(utc); g_dirty = 1; }
void app_set_tz(int32_t off)          { rtc_set_tz(off);    g_dirty = 1; }
void app_set_message(const char *msg)
{
    strncpy(g_message, msg, sizeof g_message - 1);
    g_message[sizeof g_message - 1] = '\0';
    g_dirty = 1;
}
void app_set_mode_name(const char *n)
{
    if      (!strcmp(n, "ANALOG"))  g_mode = MODE_ANALOG;
    else if (!strcmp(n, "DIGITAL")) g_mode = MODE_DIGITAL;
    else if (!strcmp(n, "MESSAGE")) g_mode = MODE_MESSAGE;
    else if (!strcmp(n, "TEST"))    g_mode = MODE_TEST;
    g_dirty = 1;
}

static void fpu_enable(void)
{
    SCB_CPACR |= (3u << 20) | (3u << 22);   /* CP10/CP11 full access */
}

static void clock_and_systick(void)
{
#if defined(BOARD_NUCLEO_F401RE)
    /* Nucleo-64 ships without an HSE crystal (it is fed from the ST-Link MCO
     * only if the solder bridges are moved), so run the PLL off the internal
     * HSI: 84 MHz sysclk, APB1 /2 = 42 MHz, timer clock x2 = 84 MHz. */
    rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_3V3_84MHZ]);
    const uint32_t ahb_hz = 84000000u;
#elif defined(BOARD_NUCLEO_G431KB) || defined(BOARD_NUCLEO_G491RE)
    /* These Nucleos have no HSE crystal either.  170 MHz needs boost mode and the
     * right flash latency; libopencm3's config table handles that sequence. */
    rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_3V3_170MHZ]);
    const uint32_t ahb_hz = 170000000u;
#else
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
    const uint32_t ahb_hz = 168000000u;
#endif
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
    systick_set_reload((ahb_hz / 8u / 1000u) - 1u);        /* 1 kHz */
    systick_interrupt_enable();
    systick_counter_enable();
}

/* User button; returns 1 on a debounced press edge.
 * Boards without one (Nucleo-32 has only RESET) change mode over the VCP. */
#if !BOARD_HAS_BUTTON
static int button_pressed(void) { return 0; }
#else
static int button_pressed(void)
{
    static uint32_t last_ms;
    static int prev;
    int raw = gpio_get(BUTTON_PORT, BUTTON_PIN) ? 1 : 0;
    int now = BUTTON_ACTIVE_HIGH ? raw : !raw;
    int edge = 0;
    if (now != prev && (rtc_millis() - last_ms) > 30) {
        last_ms = rtc_millis();
        if (now && !prev) edge = 1;
        prev = now;
    } else if (now == prev) {
        last_ms = rtc_millis();
    }
    return edge;
}
#endif

static void rebuild(void)
{
    int h, m, s, Y, Mo, D, wd;
    float frac;
    rtc_localtime(&h, &m, &s, &Y, &Mo, &D, &wd, &frac);

    scene_mode_t mode = g_mode;
    const char *msg = g_message;
    char date[16];
    snprintf(date, sizeof date, "%s %02d", MONTHS[(Mo - 1) % 12], D);

    /* No "WAIT NTP" screen any more: the timebase starts at
     * RTC_DEFAULT_EPOCH and runs, so this is a working clock from reset and a
     * later T= just replaces the time.  It also means MODE=TEST is honoured
     * before any sync, which is what you want when framing on the scope --
     * the old override swallowed every mode request until a T= arrived. */

    frame_t *fb = &frames[build_idx];
    const char *date_arg = (mode == MODE_DIGITAL) ? date : NULL;
    scene_build(fb, mode, h, m, s, frac, date_arg, msg);
    display_present(fb);
    build_idx ^= 1;
    g_dirty = 0;
}

int main(void)
{
    fpu_enable();
    clock_and_systick();

#if BOARD_HAS_BUTTON
    rcc_periph_clock_enable(BUTTON_RCC);
    /* An active-low button (the Nucleo B1) shorts to ground, so it needs the
     * internal pull-up; an active-high one (Discovery) drives both rails. */
    gpio_mode_setup(BUTTON_PORT, GPIO_MODE_INPUT,
                    BUTTON_ACTIVE_HIGH ? GPIO_PUPD_NONE : GPIO_PUPD_PULLUP,
                    BUTTON_PIN);
#endif

    /* initial active frame: bring-up test pattern */
    scene_build(&frames[0], MODE_TEST, 0, 0, 0, 0.0f, NULL, NULL);
    display_init(&frames[0]);

    rtc_init();
    esplink_init();

    for (;;) {
        if (rtc_tick())
            g_dirty = 1;
        if (button_pressed()) {
            g_mode = (scene_mode_t)((g_mode + 1) % MODE_COUNT);
            g_dirty = 1;
        }
        if (g_dirty && !display_swap_pending())
            rebuild();
        __asm__ volatile ("wfi");
    }
}
