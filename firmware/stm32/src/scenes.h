/* scenes.h -- build a frame for a given display mode + time.
 * Reference: tools/scenes.py.  Clock angle: 0 at 12, clockwise.
 */
#pragma once
#include "vector_engine.h"

typedef enum {
    MODE_ANALOG = 0,
    MODE_DIGITAL,
    MODE_MESSAGE,
    MODE_TEST,
    MODE_COUNT
} scene_mode_t;

/* Build `f` for `mode`.  date_str / message may be NULL. frac = sub-second. */
void scene_build(frame_t *f, scene_mode_t mode, int h, int m, int s, float frac,
                 const char *date_str, const char *message);
