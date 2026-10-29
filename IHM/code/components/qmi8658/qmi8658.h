/*
 * qmi8658.h — Driver minimal QMI8658 (accelerometre uniquement)
 * Utilise le bus I2C configure par i2c_bsp.
 */

#ifndef QMI8658_H
#define QMI8658_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Orientation detectee par l'accelerometre (4 positions) */
typedef enum {
    QMI_ORIENT_PORTRAIT_0,      /* portrait normal (0°) */
    QMI_ORIENT_LANDSCAPE_90,    /* paysage CW (90°) */
    QMI_ORIENT_PORTRAIT_180,    /* portrait retourne (180°) */
    QMI_ORIENT_LANDSCAPE_270,   /* paysage CCW (270°) */
    QMI_ORIENT_UNKNOWN,
} qmi8658_orient_t;

/* Donnees brutes accelerometre (mg) */
typedef struct {
    float ax;   /* mg */
    float ay;   /* mg */
    float az;   /* mg */
} qmi8658_accel_t;

/* Init le QMI8658 en mode accelerometre basse consommation.
 * Retourne true si le capteur repond correctement (WHO_AM_I = 0x05). */
bool qmi8658_init(void);

/* Lire les 3 axes accelerometre.
 * Retourne true si la lecture I2C a reussi. */
bool qmi8658_read_accel(qmi8658_accel_t *out);

/* Determiner l'orientation a partir des donnees accel.
 * Utilise un seuil de ~0.5g pour eviter les oscillations. */
qmi8658_orient_t qmi8658_get_orientation(const qmi8658_accel_t *accel);

#ifdef __cplusplus
}
#endif

#endif /* QMI8658_H */
