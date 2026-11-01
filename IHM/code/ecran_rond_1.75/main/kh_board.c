#include "kh_board.h"

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bsp.h"

static const char *TAG = "kh_board";

/* ------------------------------------------------------------------ CST9217 */
#define CST92XX_READ_CMD 0xD000
#define CST92XX_ACK 0xAB

static bool s_touch_ok;

bool kh_touch_init(void)
{
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_TP_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_config_t irq = {.pin_bit_mask = 1ULL << PIN_TP_INT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&irq);
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    uint8_t w[2] = {CST92XX_READ_CMD >> 8, CST92XX_READ_CMD & 0xFF}, r[15];
    s_touch_ok = I2C_master_write_read_device(I2C_ADDR_CST9217, w, 2, r, sizeof r) == 0;
    ESP_LOGI(TAG, "CST9217 %s", s_touch_ok ? "présent" : "absent");
    return s_touch_ok;
}

bool kh_touch_read(uint16_t *x, uint16_t *y)
{
    if (!s_touch_ok) return false;
    uint8_t w[3] = {CST92XX_READ_CMD >> 8, CST92XX_READ_CMD & 0xFF, CST92XX_ACK};
    uint8_t r[15] = {0};
    if (I2C_master_write_read_device(I2C_ADDR_CST9217, w, 2, r, sizeof r) != 0) return false;
    /* acquittement de lecture attendu par le contrôleur */
    I2C_writr_buff(I2C_ADDR_CST9217, w[0], &w[1], 2);
    if (r[6] != CST92XX_ACK) return false;
    uint8_t n = r[5] & 0x7F;
    if (n == 0 || n > 2) return false;
    if ((r[0] & 0x0F) != 0x06) return false; /* premier doigt : état « appuyé » */
    uint16_t rx = (uint16_t)((r[1] << 4) | (r[3] >> 4));
    uint16_t ry = (uint16_t)((r[2] << 4) | (r[3] & 0x0F));
    if (rx >= LCD_W || ry >= LCD_H) return false;
    *x = (uint16_t)(LCD_W - 1 - rx); /* miroir X et Y, comme bsp_display_start() */
    *y = (uint16_t)(LCD_H - 1 - ry);
    return true;
}

/* ------------------------------------------------------------------ TCA9554 */
#define TCA_IN 0x00
#define TCA_OUT 0x01
#define TCA_CFG 0x03

static bool s_exp_ok;

bool kh_expander_init(void)
{
    uint8_t v;
    s_exp_ok = I2C_read_buff(I2C_ADDR_TCA9554, TCA_CFG, &v, 1) == 0;
    ESP_LOGI(TAG, "TCA9554 %s (config 0x%02X)", s_exp_ok ? "présent" : "absent", s_exp_ok ? v : 0);
    return s_exp_ok;
}

bool kh_pwr_pressed(void)
{
    if (!s_exp_ok) return false;
    uint8_t v = 0;
    if (I2C_read_buff(I2C_ADDR_TCA9554, TCA_IN, &v, 1) != 0) return false;
    return (v >> EXIO_PWR_BTN) & 1u;
}
