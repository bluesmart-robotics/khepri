/* Tests sur PC de la molette par accéléromètre :  make -C tests && ./tests/test_knob */
#include <math.h>
#include <stdio.h>

#include "knob.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* carte debout tournée d'un angle a (degrés) dans le plan de l'écran */
static int feed_rot(knob_t *k, float from, float to, int n)
{
    int s = 0;
    for (int i = 0; i <= n; i++) {
        float a = (from + (to - from) * i / n) / 57.29578f;
        s += knob_update(k, 1000.0f * sinf(a), 1000.0f * cosf(a), 50.0f, 20);
    }
    for (int i = 0; i < 20; i++) s += knob_update(k, 1000.0f * sinf(to / 57.29578f), 1000.0f * cosf(to / 57.29578f), 50.0f, 20);
    return s;
}

/* carte à plat inclinée latéralement de t degrés pendant ms */
static int feed_tilt(knob_t *k, float t, int ms)
{
    int s = 0;
    for (int i = 0; i < ms / 20; i++) s += knob_update(k, 1000.0f * sinf(t / 57.29578f), 0.0f, 1000.0f * cosf(t / 57.29578f), 20);
    return s;
}

int main(void)
{
    knob_cfg_t cfg = {.detent_deg = 20, .tilt_on_deg = 15, .tilt_off_deg = 8, .repeat_ms = 450};
    knob_t k;
    knob_init(&k, &cfg);
    CHECK(feed_rot(&k, 0, 0, 5) == 0);       /* prise en main : pas de saut */
    CHECK(feed_rot(&k, 0, 90, 50) == 4);     /* 90° → 4 crans de 20° */
    CHECK(feed_rot(&k, 90, -5, 50) == -4);  /* retour un peu au-delà du départ : -4 crans */
    knob_init(&k, &cfg);
    CHECK(feed_rot(&k, 170, 170, 3) == 0);
    CHECK(feed_rot(&k, 170, 215, 40) == 2);  /* traverse ±180° */
    CHECK(!k.tilt_mode);

    knob_t f;
    knob_init(&f, &cfg);
    CHECK(feed_tilt(&f, 0, 500) == 0 && f.tilt_mode);
    CHECK(feed_tilt(&f, 5, 500) == 0);       /* zone morte */
    int s = feed_tilt(&f, 25, 1000);         /* 1 cran immédiat puis répétition toutes les 450 ms */
    CHECK(s >= 2 && s <= 3);
    CHECK(feed_tilt(&f, 0, 300) == 0);
    CHECK(feed_tilt(&f, -25, 200) == -1);

    cfg.invert = true;
    knob_init(&k, &cfg);
    feed_rot(&k, 0, 0, 5);
    CHECK(feed_rot(&k, 0, 45, 30) == -2);

    printf(g_fail ? "%d échec(s)\n" : "tous les tests passent\n", g_fail);
    return g_fail ? 1 : 0;
}
