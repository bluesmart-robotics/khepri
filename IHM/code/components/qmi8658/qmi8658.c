/*
 * qmi8658.c — Driver minimal QMI8658 (accelerometre)
 *
 * Registres QMI8658C :
 *   WHO_AM_I (0x00) = 0x05
 *   CTRL1    (0x02) : config SPI/I2C, auto-increment
 *   CTRL2    (0x03) : config accel (ODR, full-scale)
 *   CTRL7    (0x08) : enable accel/gyro
 *   DATA accel X/Y/Z : 0x35..0x3A (little-endian 16-bit)
 */

#include "qmi8658.h"
#include "i2c_bsp.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "QMI8658";

/* I2C address (SA0 pulled low on Waveshare board) */
#define QMI8658_ADDR        0x6B

/* Registres */
#define REG_WHO_AM_I        0x00
#define REG_REVISION        0x01
#define REG_CTRL1           0x02
#define REG_CTRL2           0x03
#define REG_CTRL3           0x04
#define REG_CTRL5           0x06
#define REG_CTRL7           0x08
#define REG_CTRL9            0x0A
#define REG_STATUSINT       0x2D
#define REG_STATUS0         0x2E
#define REG_AX_L            0x35
#define REG_RESET            0x60

/* Valeur attendue de WHO_AM_I */
#define QMI8658_WHO_AM_I_VAL  0x05

/* Sensibilite accel ±2g = 16384 LSB/g → 1 LSB = 0.061 mg */
#define ACCEL_SENSITIVITY   (1.0f / 16384.0f)  /* en g */

static bool s_initialized = false;

static bool qmi_write_reg(uint8_t reg, uint8_t val)
{
    return I2C_writr_buff(QMI8658_ADDR, reg, &val, 1) == 0;
}

static bool qmi_read_reg(uint8_t reg, uint8_t *val)
{
    return I2C_read_buff(QMI8658_ADDR, reg, val, 1) == 0;
}

static bool qmi_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return I2C_read_buff(QMI8658_ADDR, reg, buf, len) == 0;
}

bool qmi8658_init(void)
{
    uint8_t who = 0;

    /* Soft reset */
    qmi_write_reg(REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Verify WHO_AM_I */
    if (!qmi_read_reg(REG_WHO_AM_I, &who)) {
        ESP_LOGE(TAG, "I2C read WHO_AM_I failed");
        return false;
    }
    ESP_LOGI(TAG, "WHO_AM_I = 0x%02X (expected 0x%02X)", who, QMI8658_WHO_AM_I_VAL);
    if (who != QMI8658_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "QMI8658 not found!");
        return false;
    }

    /* CTRL1: address auto-increment enable */
    qmi_write_reg(REG_CTRL1, 0x40);

    /* CTRL2: Accel ±2g, ODR 250 Hz (aODR = 0101) : lu à 100 Hz pour les gestes */
    /* bits[6:4]=000 (±2g), bits[3:0]=0101 (31.25 Hz) */
    qmi_write_reg(REG_CTRL2, 0x05);

    /* CTRL3: Gyro disabled (on ne l'utilise pas) */
    qmi_write_reg(REG_CTRL3, 0x00);

    /* CTRL5: no low-pass filter */
    qmi_write_reg(REG_CTRL5, 0x00);

    /* CTRL7: enable accel only (bit 0), gyro disabled (bit 1=0) */
    qmi_write_reg(REG_CTRL7, 0x01);

    vTaskDelay(pdMS_TO_TICKS(30));

    s_initialized = true;
    ESP_LOGI(TAG, "QMI8658 initialized (accel ±2g, 31.25 Hz)");
    return true;
}

bool qmi8658_read_accel(qmi8658_accel_t *out)
{
    if (!s_initialized || !out) return false;

    uint8_t buf[6];
    if (!qmi_read_regs(REG_AX_L, buf, 6)) {
        return false;
    }

    int16_t raw_x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t raw_y = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t raw_z = (int16_t)((buf[5] << 8) | buf[4]);

    out->ax = raw_x * ACCEL_SENSITIVITY * 1000.0f;  /* mg */
    out->ay = raw_y * ACCEL_SENSITIVITY * 1000.0f;
    out->az = raw_z * ACCEL_SENSITIVITY * 1000.0f;

    return true;
}

qmi8658_orient_t qmi8658_get_orientation(const qmi8658_accel_t *accel)
{
    if (!accel) return QMI_ORIENT_UNKNOWN;

    /*
     * Waveshare ESP32-S3 AMOLED 1.8" — 4 orientations.
     * On determine l'axe dominant (|ax| vs |ay|) puis le signe
     * pour distinguer 0° vs 180° et 90° vs 270°.
     *
     * Mapping calibre :
     *   ax dominant positif → portrait 0°
     *   ax dominant negatif → portrait 180°
     *   ay dominant positif → paysage 90°
     *   ay dominant negatif → paysage 270°
     */
    float abs_ax = fabsf(accel->ax);
    float abs_ay = fabsf(accel->ay);

    /* Seuil minimum : au moins 400 mg sur un axe pour etre sur
     * que le device n'est pas a plat */
    if (abs_ax < 400.0f && abs_ay < 400.0f) {
        return QMI_ORIENT_UNKNOWN;
    }

    /* Hysteresis : il faut 150 mg de difference pour changer d'axe dominant */
    if (abs_ax > abs_ay + 150.0f) {
        /* ax dominant → portrait */
        return (accel->ax > 0) ? QMI_ORIENT_PORTRAIT_0 : QMI_ORIENT_PORTRAIT_180;
    }
    if (abs_ay > abs_ax + 150.0f) {
        /* ay dominant → paysage */
        return (accel->ay > 0) ? QMI_ORIENT_LANDSCAPE_90 : QMI_ORIENT_LANDSCAPE_270;
    }

    return QMI_ORIENT_UNKNOWN;  /* zone d'hysteresis — pas de changement */
}
