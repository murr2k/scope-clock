/* esplink.h -- UART line protocol to the ESP-01 WiFi co-processor.
 *
 * The ESP-01 is a dumb modem: it does WiFi + SNTP + the web form, then pushes
 * newline-terminated commands to the STM32 over USART2 @ 115200:
 *
 *   T=<utc_epoch>      set the clock (seconds since 1970-01-01 UTC)
 *   TZ=<offset_sec>    timezone offset in seconds (e.g. -14400 for EDT)
 *   M=<text>           custom message (<= 63 chars)
 *   MODE=<name>        ANALOG | DIGITAL | MESSAGE | TEST
 *
 * On a fully parsed line, esplink calls the app_* handlers (implemented in
 * main.c).  Parsing runs off the RX interrupt; handlers run in that context, so
 * keep them short (they just latch state + a dirty flag).
 */
#pragma once

void esplink_init(void);

/* Implemented by the application (main.c). */
void app_set_epoch(uint32_t utc);
void app_set_tz(int32_t offset_sec);
void app_set_message(const char *msg);
void app_set_mode_name(const char *name);
