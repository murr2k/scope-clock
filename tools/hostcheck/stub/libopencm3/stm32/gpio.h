/* Host-test stub for libopencm3's GPIO header.
 *
 * The board headers pull this in for the GPIOn pin-bit constants used in
 * compile-time expressions (Z-blank BSRR words, button pin).  Only those bit
 * constants are needed to compile the board-independent engine on a desktop;
 * port base addresses are never expanded outside the firmware .c files, which
 * this harness does not build.
 */
#pragma once
#define GPIO0  (1 << 0)
#define GPIO1  (1 << 1)
#define GPIO2  (1 << 2)
#define GPIO3  (1 << 3)
#define GPIO4  (1 << 4)
#define GPIO5  (1 << 5)
#define GPIO6  (1 << 6)
#define GPIO7  (1 << 7)
#define GPIO8  (1 << 8)
#define GPIO9  (1 << 9)
#define GPIO10 (1 << 10)
#define GPIO11 (1 << 11)
#define GPIO12 (1 << 12)
#define GPIO13 (1 << 13)
#define GPIO14 (1 << 14)
#define GPIO15 (1 << 15)
