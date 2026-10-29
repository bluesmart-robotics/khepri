/* Tests sur PC des gestes à l'accéléromètre :  make -C tests && ./tests/test_knob */
#include <math.h>
#include <stdio.h>

#include "knob.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

#define DT 10
/* écran face au pilote, tourné de r degrés vers la droite : ax = cos r, ay = −sin r (g = 1000 mg) */
static void at_roll(float r, float *ax, float *ay)
{
    *ax = 1000.0f * cosf(r / 57.29578f);
    *ay = -1000.0f * sinf(r / 57.29578f);
}

/* tient la position ms millisecondes ; renvoie la variation de consigne et le nombre de poussées */
static float hold(knob_t *k, float roll, float zoff, int ms, int *pushes)
{
    float s = 0, ax, ay;
    at_roll(roll, &ax, &ay);
    for (int i = 0; i < ms / DT; i++) {
        knob_evt_t e;
        knob_update(k, ax, ay, zoff, DT, &e);
        s += e.dthrottle_pct;
        if (pushes && e.push) (*pushes)++;
    }
    return s;
}

/* petit coup vers l'arrière : −600 mg pendant 60 ms puis +400 mg pendant 60 ms (freinage) */
static int push(knob_t *k)
{
    int n = 0;
    hold(k, 0, -600, 60, &n);
    hold(k, 0, 400, 60, &n);
    hold(k, 0, 0, 700, &n); /* au-delà du temps mort */
    return n;
}

int main(void)
{
    knob_cfg_t cfg = {.push_mg = 350, .push_dead_ms = 600, .roll_dead_deg = 8, .roll_full_deg = 40, .rate_max_pct_s = 40};
    knob_t k;
    knob_init(&k, &cfg);
    int n = 0;
    CHECK(fabsf(hold(&k, 0, 0, 1000, &n)) < 0.01f && n == 0); /* au repos : rien */
    CHECK(fabsf(hold(&k, 5, 0, 1500, &n)) < 0.5f);             /* zone morte */
    float up = hold(&k, 40, 0, 2000, &n);                        /* roulis à droite franc */
    CHECK(up > 50.0f && up < 85.0f);                            /* ≈ 40 %/s, filtrage au début */
    hold(&k, 0, 0, 1500, &n);                                    /* retour à plat : garde la consigne */
    float down = hold(&k, -25, 0, 1000, &n);                     /* roulis à gauche modéré ≈ 21 %/s */
    CHECK(down < -8.0f && down > -25.0f);
    CHECK(n == 0);                                               /* tourner ne déclenche pas de poussée */

    knob_init(&k, &cfg);
    hold(&k, 0, 0, 500, NULL);
    CHECK(push(&k) == 1);                                        /* un coup = une page */
    CHECK(push(&k) == 1);
    knob_init(&k, &cfg);
    hold(&k, 0, 0, 500, NULL);
    n = 0;
    hold(&k, 0, 600, 60, &n); /* tiré vers soi : pas une poussée */
    hold(&k, 0, 0, 300, &n);
    CHECK(n == 0);

    cfg.invert_roll = true;
    knob_init(&k, &cfg);
    CHECK(hold(&k, 30, 0, 1000, NULL) < -5.0f);

    /* écran à plat : roulis inutilisable, poussée toujours lue */
    knob_init(&k, &cfg);
    knob_evt_t e;
    knob_update(&k, 0, 0, 1000, DT, &e);
    CHECK(!e.roll_valid);

    printf(g_fail ? "%d échec(s)\n" : "tous les tests passent\n", g_fail);
    return g_fail ? 1 : 0;
}
