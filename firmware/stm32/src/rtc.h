/* rtc.h -- the wall-clock service: a 1 Hz timebase disciplined by SNTP.
 *
 * A software UTC epoch carried on SysTick (see rtc.c for why not the RTC
 * peripheral), plus a
 * timezone offset applied at read time.  SNTP (via the ESP-01) calls
 * rtc_set_epoch()/rtc_set_tz().  No VBAT battery in this build, so a power
 * cycle re-acquires time from SNTP on reconnect (by design).
 */
#pragma once
#include <stdint.h>

/* Power-up default: 10:10:00 UTC on Wed 26 August 2026 -- the time watches are
 * traditionally photographed at, because the hands sit symmetrically and frame
 * the dial.  The clock RUNS from here, it does not freeze, so the display is a
 * working clock from the moment of reset.  A T= from SNTP simply replaces it.
 * (Verified: this epoch round-trips to 2026-08-26 10:10:00 and the firmware's
 * own weekday formula agrees on Wednesday.) */
#define RTC_DEFAULT_EPOCH   ((uint32_t)1787739000u)

void     rtc_init(void);
uint32_t rtc_millis(void);   /* ms since boot, from SysTick */
void     rtc_set_epoch(uint32_t utc_seconds);
uint32_t rtc_get_epoch(void);
void     rtc_set_tz(int32_t offset_seconds);
int      rtc_synced(void);              /* nonzero once SNTP has set the time */

/* Consume the 1 Hz tick: returns 1 once per elapsed second. */
int      rtc_tick(void);

/* Local broken-down time (any out-param may be NULL). wday: 0=Sun..6=Sat. */
void     rtc_localtime(int *h, int *m, int *s,
                       int *year, int *mon, int *day, int *wday, float *frac);
