/*
 * bsr_battery_adc.c — Lecture niveau batterie locale via ADC1
 */

#include "bsr_battery_adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "BSR_BATT";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_calibrated = false;

/* Filtre EMA */
static float s_ema_voltage = -1.0f;  /* -1 = pas encore initialise */

void bsr_battery_adc_init(void)
{
    /* Init ADC1 oneshot */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BSR_BATT_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle));

    /* Config du canal */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BSR_BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BSR_BATT_ADC_CHANNEL, &chan_cfg));

    /* Calibration (curve fitting sur ESP32-S3) */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BSR_BATT_ADC_UNIT,
        .chan = BSR_BATT_ADC_CHANNEL,
        .atten = BSR_BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle) == ESP_OK) {
        s_calibrated = true;
        ESP_LOGI(TAG, "ADC calibre (curve fitting)");
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = BSR_BATT_ADC_UNIT,
        .atten = BSR_BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali_handle) == ESP_OK) {
        s_calibrated = true;
        ESP_LOGI(TAG, "ADC calibre (line fitting)");
    }
#endif

    if (!s_calibrated) {
        ESP_LOGW(TAG, "ADC non calibre — les lectures seront approximatives");
    }

    ESP_LOGI(TAG, "ADC batterie init sur GPIO %d", BSR_BATT_ADC_GPIO);
}

float bsr_battery_get_voltage(void)
{
    if (!s_adc_handle) return 0.0f;

    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, BSR_BATT_ADC_CHANNEL, &raw));

    float voltage_adc;
    if (s_calibrated) {
        int mv = 0;
        adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
        voltage_adc = mv / 1000.0f;
    } else {
        /* Approximation sans calibration : 12 bits, ~3.1V max a 12dB atten */
        voltage_adc = (raw / 4095.0f) * 3.1f;
    }

    float v_batt = voltage_adc * BSR_BATT_DIVIDER_RATIO;

    /* Filtre EMA : premiere lecture = initialisation directe */
    if (s_ema_voltage < 0.0f) {
        s_ema_voltage = v_batt;
    } else {
        s_ema_voltage = BSR_BATT_EMA_ALPHA * v_batt + (1.0f - BSR_BATT_EMA_ALPHA) * s_ema_voltage;
    }

    return s_ema_voltage;
}

uint8_t bsr_battery_get_percent(void)
{
    float v = bsr_battery_get_voltage();

    if (v >= BSR_BATT_V_MAX) return 100;
    if (v <= BSR_BATT_V_MIN) return 0;

    float pct = (v - BSR_BATT_V_MIN) / (BSR_BATT_V_MAX - BSR_BATT_V_MIN) * 100.0f;
    return (uint8_t)pct;
}
