/*
 * bsr_battery_adc.h — Lecture niveau batterie locale 3.7V via ADC
 *
 * Utilise le diviseur resistif integre (200K/100K) sur GPIO 4 (ADC1_CH3)
 * de la carte Waveshare ESP32-S3 Touch AMOLED 1.64
 * pour lire la tension d'une batterie LiPo 1S (3.3V–4.10V).
 *
 * Filtre EMA (alpha=0.05) pour lisser les variations.
 * Seuils calibres sur tension de repos (pas sous charge) :
 *   4.10V = 100% (repos apres charge complete)
 *   3.30V = 0%   (seuil de securite)
 */

#ifndef BSR_BATTERY_ADC_H
#define BSR_BATTERY_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* GPIO et canal ADC pour la mesure batterie
 * Waveshare ESP32-S3 Touch AMOLED 1.64 : GPIO 4 = ADC1_CH3 */
#define BSR_BATT_ADC_GPIO       4           /* GPIO 4 = ADC1_CH3 */
#define BSR_BATT_ADC_CHANNEL    ADC_CHANNEL_3
#define BSR_BATT_ADC_UNIT       ADC_UNIT_1
#define BSR_BATT_ADC_ATTEN      ADC_ATTEN_DB_12  /* plage 0–3.1V environ */

/* Diviseur resistif integre sur la carte : R1=200K (vers batt), R2=100K (vers GND)
 * Vout = Vbatt * R2 / (R1 + R2) = Vbatt / 3 */
#define BSR_BATT_DIVIDER_RATIO  3.0f

/* Seuils tension batterie LiPo 1S (repos, pas sous charge) */
#define BSR_BATT_V_MAX          4.10f       /* 100% — tension repos apres charge */
#define BSR_BATT_V_MIN          3.30f       /* 0%   — seuil securite */

/* Filtre EMA : alpha faible = lissage fort */
#define BSR_BATT_EMA_ALPHA      0.05f

/* Initialise l'ADC pour la lecture batterie. Appeler une seule fois. */
void bsr_battery_adc_init(void);

/* Lit la tension batterie en volts (apres correction diviseur). */
float bsr_battery_get_voltage(void);

/* Retourne le pourcentage batterie (0–100). */
uint8_t bsr_battery_get_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* BSR_BATTERY_ADC_H */
