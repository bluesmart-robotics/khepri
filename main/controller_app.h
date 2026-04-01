/*
 * controller_app.h — Application telecommande robot
 *
 * Encapsule l'init/deinit de :
 *   - UI LVGL telecommande (bsr_screen)
 *   - ESP-NOW (wifi, espnow, boutons, responder, callbacks)
 *
 * Appele par main.c lors des transitions de role.
 */

#ifndef CONTROLLER_APP_H
#define CONTROLLER_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Demarre le role telecommande (UI + ESP-NOW + boutons).
 * Le mutex LVGL doit etre disponible mais PAS pris par l'appelant. */
void controller_app_start(void);

/* Arrete proprement le role telecommande :
 * supprime l'UI LVGL, deinit ESP-NOW, supprime les boutons. */
void controller_app_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLLER_APP_H */
