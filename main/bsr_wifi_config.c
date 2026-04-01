/*
 * bsr_wifi_config.c — Stockage WiFi en NVS
 */

#include "bsr_wifi_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BSR_WIFI_CFG";
#define NVS_NAMESPACE "bsr_wifi"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

void bsr_wifi_config_init(void)
{
    /* NVS doit etre deja initialise dans main.c (nvs_flash_init) */
    ESP_LOGI(TAG, "WiFi config module init (NVS ready)");
}

bool bsr_wifi_config_save(const char *ssid, const char *password)
{
    if (!ssid || !password || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID vide");
        return false;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open echoue: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) goto fail;

    err = nvs_set_str(h, NVS_KEY_PASS, password);
    if (err != ESP_OK) goto fail;

    err = nvs_commit(h);
    if (err != ESP_OK) goto fail;

    nvs_close(h);
    ESP_LOGI(TAG, "WiFi sauvegarde: SSID='%s'", ssid);
    return true;

fail:
    ESP_LOGE(TAG, "Erreur NVS: %s", esp_err_to_name(err));
    nvs_close(h);
    return false;
}

bool bsr_wifi_config_load(char *ssid, size_t ssid_len,
                          char *password, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return false;

    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) { nvs_close(h); return false; }

    err = nvs_get_str(h, NVS_KEY_PASS, password, &pass_len);
    if (err != ESP_OK) { nvs_close(h); return false; }

    nvs_close(h);
    ESP_LOGI(TAG, "WiFi charge: SSID='%s'", ssid);
    return true;
}

bool bsr_wifi_config_exists(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return false;

    size_t len = 0;
    err = nvs_get_str(h, NVS_KEY_SSID, NULL, &len);
    nvs_close(h);
    return (err == ESP_OK && len > 1);
}

void bsr_wifi_config_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return;

    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "WiFi config effacee");
}
