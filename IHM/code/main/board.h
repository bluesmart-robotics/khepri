/*
 * board.h — Waveshare ESP32-S3-Touch-AMOLED-1.75 (-B / -G) : brochage utilisé par l'écran pilote.
 * Source : HARDWARE_REFERENCE.md et BSP Waveshare (esp32_s3_touch_amoled_1_75.h).
 */
#pragma once

#include "driver/gpio.h"

/* Écran AMOLED 1,75" 466×466, contrôleur CO5300 en QSPI */
#define PIN_LCD_CS    GPIO_NUM_12
#define PIN_LCD_PCLK  GPIO_NUM_38
#define PIN_LCD_D0    GPIO_NUM_4
#define PIN_LCD_D1    GPIO_NUM_5
#define PIN_LCD_D2    GPIO_NUM_6
#define PIN_LCD_D3    GPIO_NUM_7
#define PIN_LCD_RST   GPIO_NUM_39
#define PIN_LCD_TE    GPIO_NUM_13
#define LCD_W         466
#define LCD_H         466
#define LCD_X_GAP     6 /* la dalle commence à la colonne 6 du CO5300 */

/* Tactile CST9217 (I2C 0x5A) */
#define PIN_TP_INT    GPIO_NUM_11
#define PIN_TP_RST    GPIO_NUM_40

/* Bus I2C commun : SCL 14, SDA 15 (voir components/i2c_bsp) */
#define I2C_ADDR_TCA9554 0x20
#define I2C_ADDR_QMI8658 0x6B
#define I2C_ADDR_CST9217 0x5A

/* Extenseur TCA9554 */
#define EXIO_PWR_BTN  4 /* P4 : SYS_OUT, haut tant que PWR est appuyé */
#define EXIO_GPS_RST  7 /* P7 : reset du LC76G (version -G) */
