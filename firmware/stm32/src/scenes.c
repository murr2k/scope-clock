/* scenes.c -- see scenes.h.  Reference: tools/scenes.py */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "scenes.h"
#include "font_vec.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#define TWO_PI (2.0f * (float)M_PI)

static float fminf_(float a, float b) { return a < b ? a : b; }

static float fit_scale(const char *s, float max_w, float max_scale)
{
    float w = ve_text_width(s, 1.0f, 0.0f);
    return w > 0.0f ? fminf_(max_scale, max_w / w) : max_scale;
}

static void ctext(const char *s, float scale, float cyc)
{
    float w = ve_text_width(s, scale, 0.0f);
    float baseline = cyc - ((float)FONT_CAP_HEIGHT * 0.5f) * scale;
    ve_text(-w * 0.5f, baseline, s, scale, 0.0f);
}

static void hand(float a, float len, float width, float tail)
{
    float dx = sinf(a), dy = cosf(a);
    float px = cosf(a), py = -sinf(a);
    if (width <= 0.0f) {
        float p[4] = { -dx * tail, -dy * tail, dx * len, dy * len };
        ve_poly(p, 2, 0);
        return;
    }
    float p[10] = {
        -dx * tail,   -dy * tail,
         px * width,   py * width,
         dx * len,     dy * len,
        -px * width,  -py * width,
        -dx * tail,   -dy * tail,
    };
    ve_poly(p, 5, 0);
}

static void build_analog(int h, int m, int s, float frac)
{
    const float R = 0.90f;
    ve_circle(0.0f, 0.0f, R, 72);

    for (int i = 0; i < 60; i++) {
        /* On SRAM-tight boards only the 12 five-minute ticks are drawn; the
         * 48 in-between ones cost ~380 points of frame budget. */
        if (!SCENE_MINUTE_TICKS && (i % 5) != 0)
            continue;
        float a = TWO_PI * (float)i / 60.0f;
        float dx = sinf(a), dy = cosf(a);
        float r_in = (i % 15 == 0) ? R - 0.13f
                   : (i % 5 == 0)  ? R - 0.09f
                                   : R - 0.045f;
        ve_move(dx * R, dy * R);
        ve_line(dx * r_in, dy * r_in);
    }

    static const char *nums[4] = { "12", "3", "6", "9" };
    static const int   nh[4]   = { 12, 3, 6, 9 };
    for (int i = 0; i < 4; i++) {
        float a = TWO_PI * (float)(nh[i] % 12) / 12.0f;
        float dx = sinf(a), dy = cosf(a);
        float rr = R - 0.24f, sc = 0.011f;
        float w = ve_text_width(nums[i], sc, 0.0f);
        ve_text(dx * rr - w * 0.5f,
                dy * rr - ((float)FONT_CAP_HEIGHT * 0.5f) * sc,
                nums[i], sc, 0.0f);
    }

    float a_h = TWO_PI * ((float)(h % 12) / 12.0f + (float)m / 720.0f
                          + (float)s / 43200.0f);
    float a_m = TWO_PI * ((float)m / 60.0f + (float)s / 3600.0f);
    float a_s = TWO_PI * (((float)s + frac) / 60.0f);

    hand(a_h, 0.46f, 0.045f, 0.12f);
    hand(a_m, 0.74f, 0.032f, 0.12f);
    hand(a_s, 0.82f, 0.0f,   0.20f);

    ve_circle(0.0f, 0.0f, 0.03f, 10);
    ve_move(0.0f, 0.0f);
    ve_dwell(6);
}

static void build_digital(int h, int m, int s,
                          const char *date_str, const char *message)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d:%02d", h, m, s);
    int extra = (date_str && date_str[0]) || (message && message[0]);
    float main_scale = fit_scale(buf, 1.75f, 0.016f);
    ctext(buf, main_scale, extra ? 0.28f : 0.0f);

    if (date_str && date_str[0]) {
        float sc = fit_scale(date_str, 1.4f, 0.010f);
        ctext(date_str, sc, -0.28f);
    }
    if (message && message[0]) {
        float sc = fit_scale(message, 1.7f, 0.011f);
        ctext(message, sc, (date_str && date_str[0]) ? -0.62f : -0.35f);
    }
}

static void build_message(const char *message)
{
    const char *t = (message && message[0]) ? message : "SCOPE CLOCK";
    float sc = fit_scale(t, 1.8f, 0.020f);
    ctext(t, sc, 0.0f);
}

static void build_test(void)
{
    float box[10] = { -0.95f, -0.95f,  0.95f, -0.95f,  0.95f, 0.95f,
                      -0.95f,  0.95f, -0.95f, -0.95f };
    ve_poly(box, 5, 0);
    ve_circle(0.0f, 0.0f, 0.8f, 64);
    ve_move(-0.9f, 0.0f); ve_line(0.9f, 0.0f);
    ve_move(0.0f, -0.9f); ve_line(0.0f, 0.9f);
}

void scene_build(frame_t *f, scene_mode_t mode, int h, int m, int s, float frac,
                 const char *date_str, const char *message)
{
    ve_begin(f);
    switch (mode) {
    case MODE_ANALOG:  build_analog(h, m, s, frac);            break;
    case MODE_DIGITAL: build_digital(h, m, s, date_str, message); break;
    case MODE_MESSAGE: build_message(message);                 break;
    case MODE_TEST:    build_test();                           break;
    default:           build_analog(h, m, s, frac);            break;
    }
    ve_end();
}
