/* rtc.c -- the wall-clock service, kept by SysTick rather than the RTC block.
 *
 * WHY NOT THE HARDWARE RTC
 * ------------------------
 * This was originally written against the STM32F4's RTC (RM0090) with
 * hand-coded register addresses.  Moving to the G4 that code did not merely
 * misbehave, it hard-faulted: the F4 puts RCC at 0x4002_3800, the G4 at
 * 0x4002_1000, so the very first write in rtc_init() hit unmapped memory.
 * Confirmed on hardware -- CFSR=0x8200 (precise bus error), BFAR=0x40023840,
 * which is exactly the F4's RCC_APB1ENR.  The CPU sat in the HardFault handler
 * forever, and because HardFault outranks SysTick the whole main loop stopped.
 * (The picture kept streaming regardless: the DAC/DMA are autonomous.)
 *
 * libopencm3 has no RTC support for the G4 at all (there is no g4/rtc.h, only
 * rtc_common_l1f024.h for L1/F0/F2/F4), so a correct port would mean more
 * hand-written family-specific register pokes -- the exact thing that broke.
 *
 * SysTick instead: it is part of the Cortex-M core, so it is identical on every
 * target, and it is clocked from the system clock (HSI, ~1%) rather than the
 * LSI (~5%) this build was actually using.  It is therefore both simpler AND
 * more accurate here.  What is given up is the RTC's low-power/VBAT hold-over,
 * which this design already does without: there is no coin cell, and SNTP
 * re-acquires the time after a power cycle by design.
 *
 * If VBAT hold-over is ever wanted, this is the file to reimplement per family.
 */
#include <libopencm3/cm3/systick.h>
#include "rtc.h"

static volatile uint32_t g_millis;      /* since boot, 1 kHz from SysTick */
static uint32_t g_epoch_at_base;        /* UTC seconds at g_ms_base */
static uint32_t g_ms_base;
static uint32_t g_last_second;
static int32_t  g_tz;
static int      g_synced;

void sys_tick_handler(void);
void sys_tick_handler(void) { g_millis++; }

uint32_t rtc_millis(void) { return g_millis; }

void rtc_init(void)
{
    /* SysTick is started in main()'s clock setup; nothing else to do. */
    g_epoch_at_base = RTC_DEFAULT_EPOCH;
    g_ms_base = g_millis;
    g_last_second = 0;
}

uint32_t rtc_get_epoch(void)
{
    return g_epoch_at_base + (uint32_t)((g_millis - g_ms_base) / 1000u);
}

void rtc_set_epoch(uint32_t utc)
{
    g_epoch_at_base = utc;
    g_ms_base = g_millis;
    g_synced = 1;
}

void rtc_set_tz(int32_t off) { g_tz = off; }
int  rtc_synced(void)        { return g_synced; }

/* Returns 1 once per elapsed second. */
int rtc_tick(void)
{
    uint32_t s = rtc_get_epoch();
    if (s != g_last_second) {
        g_last_second = s;
        return 1;
    }
    return 0;
}

/* civil date from days since 1970-01-01 (Hinnant's algorithm) */
static void civil_from_days(int32_t z, int *y, int *m, int *d)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp < 10 ? mp + 3 : mp - 9;
    *y = yy + (mm <= 2);
    *m = (int)mm;
    *d = (int)dd;
}

void rtc_localtime(int *h, int *m, int *s,
                   int *year, int *mon, int *day, int *wday, float *frac)
{
    uint32_t now = rtc_get_epoch();
    int32_t local = (int32_t)now + g_tz;
    int32_t days = local / 86400;
    int32_t secs = local % 86400;
    if (secs < 0) { secs += 86400; days -= 1; }
    if (h) *h = secs / 3600;
    if (m) *m = (secs % 3600) / 60;
    if (s) *s = secs % 60;
    int Y, Mo, D;
    civil_from_days(days, &Y, &Mo, &D);
    if (year) *year = Y;
    if (mon)  *mon = Mo;
    if (day)  *day = D;
    if (wday) *wday = (int)(((days % 7) + 4 + 7) % 7);   /* 1970-01-01 = Thu */
    /* Sub-second phase, free now that the timebase is milliseconds: this is
     * what a smooth sweeping second hand would ride on. */
    if (frac) *frac = (float)((g_millis - g_ms_base) % 1000u) / 1000.0f;
}
