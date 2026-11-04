/*
 * kh_board.h — pilotes minimaux de la carte 1,75" : tactile CST9217 et extenseur TCA9554.
 * Utilisent le bus I2C de components/i2c_bsp (pilote I2C historique, protégé par mutex).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tactile : reset matériel puis lecture par interrogation (protocole CST92xx de SensorLib) */
bool kh_touch_init(void);
/* true si un doigt est posé ; coordonnées écran 0..465 (miroir X/Y comme le BSP Waveshare) */
bool kh_touch_read(uint16_t *x, uint16_t *y);

/* Extenseur : true si le bouton PWR est appuyé (TCA9554 P4 sur Waveshare, BOOT/GPIO0 sur la carte BSR) */
bool kh_expander_init(void);
bool kh_pwr_pressed(void);

/* Carte BSR : UART1 vers la carte façade (GPIO17 TX / GPIO18 RX). Transport seulement : le
 * protocole IHM (relais de la télémétrie et des commandes, aujourd'hui en ESP-NOW) reste à brancher
 * dessus. Sans effet sur la Waveshare. */
void kh_facade_uart_init(void);
int kh_facade_uart_write(const void *buf, size_t len);
int kh_facade_uart_read(void *buf, size_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
