#include "knob.h"

#include <math.h>
#include <string.h>

#define RAD2DEG 57.29578f
#define LP_SLOW 0.04f   /* Z de référence pour la poussée (≈ 0,25 s à 100 Hz) */
#define LP_ROLL 0.12f   /* X-Y pour le roulis : réactif (≈ 80 ms), le bruit est absorbé par la zone morte */
#define LP_FAST 0.6f    /* Z « instantané », débarrassé du bruit */
#define G_ROLL_MIN 0.6f /* composante de gravité dans le plan X-Y nécessaire au roulis [g] */

void knob_init(knob_t *k, const knob_cfg_t *cfg)
{
    memset(k, 0, sizeof *k);
    k->cfg = *cfg;
    if (k->cfg.roll_full_deg <= k->cfg.roll_dead_deg) k->cfg.roll_full_deg = k->cfg.roll_dead_deg + 10.0f;
}

void knob_update(knob_t *k, float ax, float ay, float az, uint32_t dt_ms, knob_evt_t *out)
{
    memset(out, 0, sizeof *out);
    if (!k->init) {
        k->ax = ax, k->ay = ay, k->az = az, k->zfast = az;
        k->init = true;
    }
    k->ax += LP_ROLL * (ax - k->ax);
    k->ay += LP_ROLL * (ay - k->ay);
    k->az += LP_SLOW * (az - k->az);
    k->zfast += LP_FAST * (az - k->zfast);

    /* --- poussée vers l'arrière : l'écran part vers −Z, l'accéléromètre lit une baisse brève de Z */
    float hz = k->zfast - k->az;
    if (k->cfg.invert_push) hz = -hz;
    if (k->dead_left > dt_ms) {
        k->dead_left -= dt_ms;
    } else {
        k->dead_left = 0;
        if (hz < -k->cfg.push_mg) {
            out->push = true;
            k->dead_left = k->cfg.push_dead_ms;
        }
    }

    /* --- roulis dans le plan de l'écran (sur la gravité filtrée : insensible aux coups) */
    float gxy = sqrtf(k->ax * k->ax + k->ay * k->ay) / 1000.0f;
    if (gxy < G_ROLL_MIN) return;
    float roll = atan2f(-k->ay, k->ax) * RAD2DEG; /* + : sommet de l'écran vers la droite */
    if (k->cfg.invert_roll) roll = -roll;
    out->roll_valid = true;
    out->roll_deg = roll;
    float a = fabsf(roll);
    if (a > k->cfg.roll_dead_deg) {
        float f = (fminf(a, k->cfg.roll_full_deg) - k->cfg.roll_dead_deg) / (k->cfg.roll_full_deg - k->cfg.roll_dead_deg);
        float d = f * k->cfg.rate_max_pct_s * (float)dt_ms / 1000.0f;
        out->dthrottle_pct = roll > 0 ? d : -d;
    }
}
