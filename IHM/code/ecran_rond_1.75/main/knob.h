/*
 * knob.h — gestes à l'accéléromètre QMI8658, en attendant le bouton haptique (moteur BLDC).
 *
 * Repère de la carte, écran face au pilote : X vers le haut, Y vers la droite, Z sort de l'écran
 * vers le pilote. Au repos, l'accéléromètre lit +1 g sur l'axe tourné vers le haut.
 *
 *  - POUSSÉE : un petit coup qui pousse l'écran vers l'arrière (vers −Z) → page suivante.
 *    Détection d'une impulsion sur Z, filtrée de la gravité (passe-haut), avec temps mort.
 *    Simule l'appui du futur bouton.
 *  - ROULIS : écran tourné dans son plan (autour de Z), vers la droite (sens horaire vu par le
 *    pilote) → la consigne de régime augmente ; vers la gauche → elle diminue. Au-delà d'une zone
 *    morte, la vitesse de variation croît avec l'angle ; revenir à plat garde la consigne.
 *    Roulis = atan2(−ay, ax) : il faut tenir l'écran à peu près debout.
 *
 * Sans dépendance ESP-IDF (testé sur PC).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float push_mg;          /* seuil de l'impulsion vers l'arrière [mg] */
    uint32_t push_dead_ms;  /* temps mort après une poussée */
    float roll_dead_deg;    /* zone morte du roulis */
    float roll_full_deg;    /* roulis donnant la vitesse maximale */
    float rate_max_pct_s;   /* vitesse maximale de variation de la consigne [%/s] */
    bool invert_roll;       /* inverse droite / gauche */
    bool invert_push;       /* inverse le sens de la poussée sur Z */
} knob_cfg_t;

typedef struct {
    bool push;              /* une poussée vient d'être détectée */
    bool roll_valid;        /* écran assez debout pour lire le roulis */
    float roll_deg;         /* roulis, + = vers la droite */
    float dthrottle_pct;    /* variation de consigne à appliquer [%] */
} knob_evt_t;

typedef struct {
    knob_cfg_t cfg;
    bool init;
    float ax, ay, az;       /* accélération filtrée (≈ gravité) [mg] */
    float zfast;            /* Z peu filtré */
    uint32_t dead_left;
} knob_t;

void knob_init(knob_t *k, const knob_cfg_t *cfg);
/* Une mesure (mg) toutes les dt_ms */
void knob_update(knob_t *k, float ax_mg, float ay_mg, float az_mg, uint32_t dt_ms, knob_evt_t *out);

#ifdef __cplusplus
}
#endif
