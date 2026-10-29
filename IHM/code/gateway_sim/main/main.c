/*
 * Passerelle de simulation KHÉPRI : joue le rôle du maître pour tester l'écran pilote sans
 * l'avion. Tourne sur n'importe quel ESP32-S3 (par exemple la carte AMOLED 1,64" de bsr_screen).
 *
 * Elle génère les mêmes trames CAN que les chaussettes et le BSR Drive (kh_demo_frames), les fait
 * passer par khepri_relay — le code destiné au vrai maître — et les envoie à l'écran appairé.
 *
 * Appairage : approcher l'écran à quelques centimètres, page SYSTÈME → LIER.
 */
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kh_model.h"
#include "khepri_relay.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "khepri_gw";

static void on_cmd(uint8_t code, uint32_t arg)
{
    switch (code) {
    case KHN_CMD_ACK_ALARM: ESP_LOGW(TAG, "le pilote a acquitté l'alarme de la chaussette %lu", (unsigned long)arg); break;
    case KHN_CMD_MARK: ESP_LOGI(TAG, "REPÈRE pilote n° %lu (sur le maître : « mark » vers les chaussettes)", (unsigned long)arg); break;
    case KHN_CMD_LOG: ESP_LOGI(TAG, "enregistrement %s demandé", arg ? "ON" : "OFF"); break;
    default: break;
    }
}

void app_main(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    espnow_storage_init();

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wc));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_KHEPRI_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    espnow_config_t ec = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&ec);

    khepri_relay_cfg_t cfg = KHEPRI_RELAY_CFG_DEFAULT();
    cfg.flags = KHN_HF_DEMO;
    cfg.on_cmd = on_cmd;
    khepri_relay_init(&cfg);

    ESP_LOGI(TAG, "passerelle de simulation prête, canal %d", CONFIG_KHEPRI_ESPNOW_CHANNEL);
    TickType_t last = xTaskGetTickCount();
    uint32_t tick = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
        khn_frame_t f[KHN_MAX_FRAMES];
        int n = kh_demo_frames(esp_timer_get_time() / 1000, f, KHN_MAX_FRAMES);
        for (int i = 0; i < n; i++) khepri_relay_push(f[i].id, f[i].data, f[i].dlc);
        if (++tick % 50 == 0)
            ESP_LOGI(TAG, "%d écran(s) appairé(s), %lu paquets envoyés", khepri_relay_displays(),
                     (unsigned long)khepri_relay_sent());
    }
}
