/*
 * battery_unplugged_app.h — Application batterie deconnectee (mode BLE)
 *
 * Encapsule l'init/deinit de l'UI LVGL batterie en mode deconnecte.
 * Affiche les memes infos que battery_app mais sans la page USB-C
 * et avec seulement 2 sondes temperature (Cellule + BMS).
 *
 * Sera enrichi avec la communication BLE vers le BMS.
 */

#ifndef BATTERY_UNPLUGGED_APP_H
#define BATTERY_UNPLUGGED_APP_H

#ifdef __cplusplus
extern "C" {
#endif

void battery_unplugged_app_start(void);
void battery_unplugged_app_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_UNPLUGGED_APP_H */
