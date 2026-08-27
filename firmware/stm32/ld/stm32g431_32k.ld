/* stm32g431_32k.ld -- claim all 32 KB of the G431KB's SRAM.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * libopencm3's auto-generated script for this part declares:
 *     ram (rwx) : ORIGIN = 0x20000000, LENGTH = 22K      <- SRAM1 + SRAM2 only
 *     ccm (rwx) : ORIGIN = 0x10000000, LENGTH = 10K
 * i.e. the 10 KB CCM SRAM is left out of `ram` and offered only at its
 * 0x10000000 mapping.  The scope clock's double-buffered frames are ~24 KB, so
 * linking against 22 KB overflows by ~2.8 KB.
 *
 * On category 2 STM32G4 parts (which the G431 is) the CCM SRAM is *aliased* at
 * 0x2000 5800, immediately after SRAM2, forming one contiguous 32 KB block
 * (RM0440 sec 2.4, and AN4296).  Two consequences matter here:
 *
 *   1. 0x20000000 + 32K is a single valid RAM region, so this script is not a
 *      lie to the linker.
 *   2. **DMA can reach the CCM only through this alias**, never through
 *      0x10000000.  Since the frame buffers are DMA sources, they must live in
 *      the aliased range -- which is exactly what claiming it as `ram` does.
 *      Putting them in a `.ccmram` section at 0x10000000 instead would compile
 *      and link fine and then fail silently at run time.
 *
 * Selected by `board_build.ldscript` in platformio.ini.
 */

MEMORY
{
	rom (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

INCLUDE cortex-m-generic.ld
