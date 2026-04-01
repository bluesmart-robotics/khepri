/*
 * device_config.h — Configuration detection appareil plug & play
 *
 * Deux GPIO connectees au connecteur physique :
 *   - PRESENT (GPIO 6) : indique si un appareil est branche ou non
 *   - SELECT  (GPIO 5) : indique quel appareil (telecommande ou batterie)
 *
 * La detection fonctionne au boot ET a chaud via interruption GPIO.
 *
 * Cablage cote connecteur :
 *   - Les DEUX connecteurs (telecommande ET batterie) tirent PRESENT a GND
 *   - Le connecteur telecommande tire SELECT a GND
 *   - Le connecteur batterie  tire SELECT a 3.3V (ou laisse flottant → pull-up → HIGH)
 *
 * Tableau de verite :
 *   PRESENT(GPIO6)  SELECT(GPIO5)   Resultat
 *   ──────────────  ─────────────   ──────────────────
 *   HIGH (1)        x               UNKNOWN (rien branche)
 *   LOW  (0)        LOW  (0)        CONTROLLER (telecommande)
 *   LOW  (0)        HIGH (1)        BATTERY (module batterie)
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/gpio.h"

/* ── GPIO de detection ──
 * GPIO 5 (SELECT)  : determine le type d'appareil
 * GPIO 6 (PRESENT) : detecte si quelque chose est branche
 *
 * Les deux ont un pull-up interne actif :
 *   - rien branche → HIGH (pull-up)
 *   - appareil branche → tire a GND via le connecteur
 * NOTE: GPIO 19/20 = USB D-/D+ sur ESP32-S3, ne pas utiliser !
 */
#define DEVICE_SELECT_GPIO      GPIO_NUM_5      /* LOW=telecommande, HIGH=batterie */
#define DEVICE_PRESENT_GPIO     GPIO_NUM_6      /* LOW=branche, HIGH=rien */
#define DEBOUNCE_MS             50              /* anti-rebond logiciel */

/* ── Roles possibles ── */
typedef enum {
    DEVICE_ROLE_CONTROLLER = 0,     /* appareil branche + SELECT LOW */
    DEVICE_ROLE_BATTERY    = 1,     /* appareil branche + SELECT HIGH */
    DEVICE_ROLE_UNKNOWN    = 0xFF   /* rien branche (PRESENT HIGH) */
} device_role_t;

/* ── Noms lisibles pour les logs ── */
static inline const char *device_role_to_string(device_role_t role)
{
    switch (role) {
        case DEVICE_ROLE_CONTROLLER: return "TELECOMMANDE";
        case DEVICE_ROLE_BATTERY:    return "BATTERIE";
        default:                     return "INCONNU";
    }
}

/* ── ESP-NOW (partage entre les deux roles) ── */
#define ESPNOW_CHANNEL          1
#define ESPNOW_PMK              "pmk1234567890123"
#define ESPNOW_LMK              "lmk1234567890123"

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_CONFIG_H */
