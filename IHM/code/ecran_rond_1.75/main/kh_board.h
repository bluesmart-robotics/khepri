/*
 * kh_board.h — pilotes minimaux de la carte 1,75" : tactile CST9217 et extenseur TCA9554.
 * Utilisent le bus I2C de components/i2c_bsp (pilote I2C historique, protégé par mutex).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tactile : reset matériel puis lecture par interrogation (protocole CST92xx de SensorLib) */
bool kh_touch_init(void);
/* true si un doigt est posé ; coordonnées écran 0..465 (miroir X/Y comme le BSP Waveshare) */
bool kh_touch_read(uint16_t *x, uint16_t *y);

/* Extenseur : true si le bouton PWR est appuyé (TCA9554 P4) */
bool kh_expander_init(void);
bool kh_pwr_pressed(void);

#ifdef __cplusplus
}
#endif
