/*
 * knob.h — Molette virtuelle à partir de l'accéléromètre QMI8658.
 *
 * En attendant le PCB où un moteur BLDC servira d'encodeur rotatif, on lit la gravité :
 *
 *  - Carte tenue debout (écran face au pilote, |g| dans le plan de l'écran > 0,6 g) :
 *    l'angle de la gravité dans le plan de l'écran donne la position d'une molette.
 *    Tourner la carte comme un bouton fait défiler les pages, un cran tous les
 *    CONFIG_KHEPRI_KNOB_DETENT_DEG degrés.
 *  - Carte à plat (< 0,45 g dans le plan) : la rotation autour de la verticale est invisible
 *    pour un accéléromètre. On passe en « manette » : incliner la carte à droite ou à gauche
 *    de plus de 15° fait un cran, puis répète tant qu'on reste incliné.
 *
 * Ce n'est qu'une approximation : l'avion lui-même accélère et s'incline. L'interface
 * knob_update() ne dépend pas du capteur ; l'encodeur BLDC la remplacera sans toucher à l'UI.
 * Sans dépendance ESP-IDF (testé sur PC).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float detent_deg;    /* angle d'un cran en mode molette */
    float tilt_on_deg;   /* inclinaison déclenchant un cran en mode manette */
    float tilt_off_deg;  /* retour au neutre */
    uint32_t repeat_ms;  /* répétition en mode manette */
    bool invert;         /* inverse le sens */
} knob_cfg_t;

typedef struct {
    knob_cfg_t cfg;
    bool init, tilt_mode, tilt_held;
    float ax, ay, az;    /* accélération filtrée [mg] */
    float anchor_deg;    /* angle du dernier cran */
    float angle_deg;     /* angle courant (molette) ou inclinaison ×4 (manette) */
    uint32_t repeat_left;
    int tilt_dir;
} knob_t;

void knob_init(knob_t *k, const knob_cfg_t *cfg);
/* Une mesure (mg) toutes les dt_ms ; retourne le nombre de crans depuis l'appel précédent. */
int knob_update(knob_t *k, float ax_mg, float ay_mg, float az_mg, uint32_t dt_ms);
/* Molette utilisable (mode debout), ou mode manette */
bool knob_valid(const knob_t *k);

#ifdef __cplusplus
}
#endif
