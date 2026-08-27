/* board.h -- select the target board.
 *
 * Each board header defines the pin map, the sample clock, the frame budget,
 * `ve_word_t`, and `frame_pack()` (how a beam sample is turned into the two
 * words the two DMA streams consume).  Everything above this layer -- the
 * vector engine, the scenes, the font, the UART protocol -- is board-agnostic.
 *
 * The PlatformIO env supplies the -D flag.
 */
#pragma once

#if defined(BOARD_NUCLEO_F401RE)
#  include "board_f401.h"
#elif defined(BOARD_NUCLEO_G431KB)
#  include "board_g431.h"
#elif defined(BOARD_NUCLEO_G491RE)
#  include "board_g491.h"
#elif defined(BOARD_WEACT_G431CB)
#  include "board_weact_g431.h"
#elif defined(BOARD_DISCO_F407VG)
#  include "board_f407.h"
#else
#  error "Define a BOARD_* target (see platformio.ini)"
#endif
