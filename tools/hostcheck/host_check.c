/* Host cross-check harness: compiles the TARGET's vector engine, scenes and
 * font with a desktop compiler and prints each scene's sample count, so the C
 * engine can be diffed against the Python reference without hardware.
 *
 * Driven by tools/check_c_engine.py -- see that file for how it is built.
 */
#include <stdio.h>
#include "scenes.h"

static frame_t f;

int main(void)
{
    struct { const char *name; scene_mode_t mode; const char *date; const char *msg; } S[] = {
        { "testpattern",  MODE_TEST,    NULL,     NULL          },
        { "analog",       MODE_ANALOG,  NULL,     NULL          },
        { "digital",      MODE_DIGITAL, NULL,     NULL          },
        { "digital_full", MODE_DIGITAL, "JUL 21", "HELLO SCOPE" },
        { "message",      MODE_MESSAGE, NULL,     "HELLO SCOPE" },
    };
    printf("board %s max_points %u\n", BOARD_NAME, (unsigned)VE_MAX_POINTS);
    for (int i = 0; i < 5; i++) {
        scene_build(&f, S[i].mode, 10, 8, 42, 0.0f, S[i].date, S[i].msg);
        printf("%s %u\n", S[i].name, (unsigned)f.n);
    }
    return 0;
}
