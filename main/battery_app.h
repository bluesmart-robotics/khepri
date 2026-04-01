/*
 * battery_app.h — Application module batterie robot
 *
 * Stub minimal pour le role batterie.
 * Sera enrichi plus tard avec UI batterie + ESP-NOW batterie.
 */

#ifndef BATTERY_APP_H
#define BATTERY_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Demarre le role module batterie (UI batterie + ESP-NOW).
 * Le mutex LVGL doit etre disponible mais PAS pris par l'appelant. */
void battery_app_start(void);

/* Arrete proprement le role module batterie. */
void battery_app_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_APP_H */
