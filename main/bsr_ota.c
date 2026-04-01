/*
 * bsr_ota.c — Mise a jour OTA via WiFi HTTP
 *
 * Lit les identifiants WiFi depuis la NVS (bsr_wifi_config),
 * se connecte, telecharge le firmware, et reboot.
 */

#include "bsr_ota.h"
#include "bsr_wifi_config.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "BSR_OTA";

/* Event group pour attendre la connexion WiFi */
static EventGroupHandle_t s_wifi_eg = NULL;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

#define WIFI_RETRY_MAX  5
static int s_wifi_retry_count = 0;

/* Callback WiFi */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_count < WIFI_RETRY_MAX) {
            s_wifi_retry_count++;
            ESP_LOGW(TAG, "WiFi deconnecte, retry %d/%d...", s_wifi_retry_count, WIFI_RETRY_MAX);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi: max retries atteint");
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP obtenue: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_count = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

/* Se connecte au WiFi OTA avec les identifiants NVS. Retourne true si connecte. */
static bool ota_wifi_connect(void)
{
    /* Lire les identifiants WiFi depuis NVS */
    char ssid[BSR_WIFI_SSID_MAX] = {0};
    char pass[BSR_WIFI_PASS_MAX] = {0};

    if (!bsr_wifi_config_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGE(TAG, "Pas de config WiFi en NVS — configurer via l'ecran WiFi");
        return false;
    }

    s_wifi_eg = xEventGroupCreate();
    s_wifi_retry_count = 0;

    /* Deconnecter d'abord si deja associe (ex: ESP-NOW) */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Configurer le WiFi STA pour se connecter a l'AP OTA */
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    /* Enregistrer les handlers APRES le disconnect pour eviter un faux FAIL_BIT */
    esp_event_handler_instance_t h_wifi = NULL, h_ip = NULL;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL, &h_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &h_ip));

    ESP_LOGI(TAG, "Connexion WiFi a '%s'...", ssid);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(BSR_OTA_WIFI_TIMEOUT_MS));

    esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, h_wifi);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h_ip);
    vEventGroupDelete(s_wifi_eg);
    s_wifi_eg = NULL;

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connecte");
        return true;
    }
    ESP_LOGE(TAG, "WiFi connexion echouee (timeout ou erreur)");
    return false;
}

/* Deconnecte du WiFi OTA */
static void ota_wifi_disconnect(void)
{
    esp_wifi_disconnect();
    ESP_LOGI(TAG, "WiFi deconnecte");
}

bool bsr_ota_start(void)
{
    ESP_LOGI(TAG, "=== Debut mise a jour OTA ===");

    /* 1. Connexion WiFi */
    if (!ota_wifi_connect()) {
        ESP_LOGE(TAG, "Impossible de se connecter au WiFi — OTA annulee");
        return false;
    }

    /* 2. Preparer la partition OTA */
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "Pas de partition OTA disponible");
        ota_wifi_disconnect();
        return false;
    }
    ESP_LOGI(TAG, "Partition cible: %s (offset 0x%lx, taille %lu)",
             update_part->label, update_part->address, update_part->size);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin echoue: %s", esp_err_to_name(err));
        ota_wifi_disconnect();
        return false;
    }

    /* 3. Telecharger le firmware via HTTP */
    esp_http_client_config_t http_cfg = {
        .url = BSR_OTA_FW_URL,
        .timeout_ms = 10000,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open echoue: %s", esp_err_to_name(err));
        esp_ota_abort(ota_handle);
        esp_http_client_cleanup(client);
        ota_wifi_disconnect();
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Firmware a telecharger: %d octets", content_length);

    char *buf = malloc(1024);
    if (!buf) {
        ESP_LOGE(TAG, "malloc echoue");
        esp_ota_abort(ota_handle);
        esp_http_client_cleanup(client);
        ota_wifi_disconnect();
        return false;
    }

    int total = 0;
    int read_len;
    while ((read_len = esp_http_client_read(client, buf, 1024)) > 0) {
        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write echoue a offset %d: %s", total, esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            ota_wifi_disconnect();
            return false;
        }
        total += read_len;
        if (total % (100 * 1024) == 0) {
            ESP_LOGI(TAG, "Progression: %d KB", total / 1024);
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ota_wifi_disconnect();

    ESP_LOGI(TAG, "Telechargement termine: %d octets", total);

    if (total == 0) {
        ESP_LOGE(TAG, "Aucune donnee recue");
        esp_ota_abort(ota_handle);
        return false;
    }

    /* 4. Finaliser et valider */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end echoue: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition echoue: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "=== OTA OK — Redemarrage sur %s ===", update_part->label);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return true;  /* jamais atteint */
}

const char *bsr_ota_get_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return desc->version;
}
