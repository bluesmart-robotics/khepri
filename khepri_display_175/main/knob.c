#include "knob.h"

#include <math.h>
#include <string.h>

#define RAD2DEG 57.29578f
#define G_KNOB_ON 0.60f  /* passe en molette au-dessus de cette composante dans le plan [g] */
#define G_KNOB_OFF 0.45f /* passe en manette en dessous */
#define LPF 0.30f        /* filtre passe-bas (≈ 50 Hz d'échantillonnage) */

static float wrap180(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

void knob_init(knob_t *k, const knob_cfg_t *cfg)
{
    memset(k, 0, sizeof *k);
    k->cfg = *cfg;
    if (k->cfg.detent_deg < 5.0f) k->cfg.detent_deg = 5.0f;
}

bool knob_valid(const knob_t *k) { return k->init; }

int knob_update(knob_t *k, float ax, float ay, float az, uint32_t dt_ms)
{
    if (!k->init) {
        k->ax = ax, k->ay = ay, k->az = az;
    } else {
        k->ax += LPF * (ax - k->ax);
        k->ay += LPF * (ay - k->ay);
        k->az += LPF * (az - k->az);
    }
    const float gxy = sqrtf(k->ax * k->ax + k->ay * k->ay) / 1000.0f;
    const float ang = atan2f(k->ax, k->ay) * RAD2DEG; /* 0° : gravité vers +Y */
    int steps = 0;

    bool was_init = k->init;
    k->init = true;
    bool tilt = k->tilt_mode;
    if (!was_init) tilt = gxy < G_KNOB_ON;
    else if (tilt && gxy > G_KNOB_ON) tilt = false;
    else if (!tilt && gxy < G_KNOB_OFF) tilt = true;

    if (!tilt) {
        if (k->tilt_mode || !was_init) k->anchor_deg = ang; /* nouvelle prise en main : pas de saut */
        k->tilt_mode = false;
        float d = wrap180(ang - k->anchor_deg);
        while (d >= k->cfg.detent_deg) {
            steps++;
            k->anchor_deg = wrap180(k->anchor_deg + k->cfg.detent_deg);
            d -= k->cfg.detent_deg;
        }
        while (d <= -k->cfg.detent_deg) {
            steps--;
            k->anchor_deg = wrap180(k->anchor_deg - k->cfg.detent_deg);
            d += k->cfg.detent_deg;
        }
        k->angle_deg = ang;
    } else {
        k->tilt_mode = true;
        /* inclinaison latérale : composante X de la gravité */
        float s = k->ax / 1000.0f;
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        float tilt_deg = asinf(s) * RAD2DEG;
        k->angle_deg = tilt_deg * 4.0f;
        int dir = tilt_deg > k->cfg.tilt_on_deg ? 1 : tilt_deg < -k->cfg.tilt_on_deg ? -1 : 0;
        if (fabsf(tilt_deg) < k->cfg.tilt_off_deg) {
            k->tilt_held = false;
            k->tilt_dir = 0;
        } else if (dir) {
            if (!k->tilt_held || dir != k->tilt_dir) {
                steps = dir;
                k->tilt_held = true;
                k->tilt_dir = dir;
                k->repeat_left = k->cfg.repeat_ms;
            } else if (k->repeat_left <= dt_ms) {
                steps = dir;
                k->repeat_left = k->cfg.repeat_ms;
            } else {
                k->repeat_left -= dt_ms;
            }
        }
    }
    return k->cfg.invert ? -steps : steps;
}
