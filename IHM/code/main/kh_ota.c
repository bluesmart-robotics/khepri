#include "kh_ota.h"

#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kh_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "kh_ota";

#define CHECK_PERIOD_MS (CONFIG_KHEPRI_OTA_PERIOD_S * 1000)
#define MANIFEST_MAX 1024

static kh_ota_status_t s_st;
static kh_ota_can_update_cb_t s_can;
static TaskHandle_t s_task;

/* ------------------------------------------------------------------ manifeste */
static bool fetch_manifest(char *ver, size_t vl, char *url, size_t ul)
{
    char *buf = calloc(1, MANIFEST_MAX + 1);
    if (!buf) return false;
    /* paramètre anti-cache : chaque requête porte une URL différente */
    char murl[256];
    snprintf(murl, sizeof murl, "%s?t=%lld", CONFIG_KHEPRI_OTA_MANIFEST_URL, (long long)(esp_timer_get_time() / 1000));
    esp_http_client_config_t cfg = {
        .url = murl,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    bool ok = false;
    if (c && esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        int n = 0, r;
        while (n < MANIFEST_MAX && (r = esp_http_client_read(c, buf + n, MANIFEST_MAX - n)) > 0) n += r;
        if (status == 200 && n > 0) {
            cJSON *j = cJSON_Parse(buf);
            const cJSON *v = cJSON_GetObjectItem(j, "version");
            const cJSON *u = cJSON_GetObjectItem(j, "url");
            if (cJSON_IsString(v) && cJSON_IsString(u)) {
                strlcpy(ver, v->valuestring, vl);
                strlcpy(url, u->valuestring, ul);
                ok = true;
            }
            cJSON_Delete(j);
        } else {
            ESP_LOGW(TAG, "manifeste : HTTP %d", status);
        }
    }
    if (c) esp_http_client_cleanup(c);
    free(buf);
    return ok;
}

/* ------------------------------------------------------------------ téléchargement */
static esp_err_t download(const char *url)
{
    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };
    esp_https_ota_config_t cfg = {.http_config = &http};
    esp_https_ota_handle_t h = NULL;
    esp_err_t e = esp_https_ota_begin(&cfg, &h);
    if (e != ESP_OK) return e;
    int total = esp_https_ota_get_image_size(h);
    while ((e = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int done = esp_https_ota_get_image_len_read(h);
        if (total > 0) s_st.progress_pct = done * 100 / total;
    }
    if (e != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        return e != ESP_OK ? e : ESP_FAIL;
    }
    return esp_https_ota_finish(h);
}

static void ota_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CHECK_PERIOD_MS));
        if (!kh_wifi_connected()) {
            s_st.state = KH_OTA_IDLE;
            continue;
        }
        char ver[32], url[200];
        s_st.last_check_ms = esp_timer_get_time() / 1000;
        if (!fetch_manifest(ver, sizeof ver, url, sizeof url)) {
            s_st.state = KH_OTA_CHECK_FAIL;
            continue;
        }
        strlcpy(s_st.remote, ver, sizeof s_st.remote);
        if (!strcmp(ver, s_st.running)) {
            s_st.state = KH_OTA_UP_TO_DATE;
            continue;
        }
        if (s_can && !s_can()) {
            s_st.state = KH_OTA_DEFERRED;
            ESP_LOGW(TAG, "version %s disponible, reportée (moteurs en marche ou alerte)", ver);
            continue;
        }
        ESP_LOGW(TAG, "mise à jour %s → %s depuis %s", s_st.running, ver, url);
        s_st.state = KH_OTA_DOWNLOADING;
        s_st.progress_pct = 0;
        esp_err_t e = download(url);
        if (e == ESP_OK) {
            ESP_LOGW(TAG, "mise à jour écrite, redémarrage");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        ESP_LOGE(TAG, "échec de la mise à jour : %s", esp_err_to_name(e));
        s_st.state = KH_OTA_FAILED;
    }
}

void kh_ota_init(kh_ota_can_update_cb_t can_update)
{
    s_can = can_update;
    strlcpy(s_st.running, esp_app_get_description()->version, sizeof s_st.running);
    const esp_partition_t *p = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "firmware %s sur la partition %s ; manifeste %s toutes les %d s", s_st.running, p ? p->label : "?",
             CONFIG_KHEPRI_OTA_MANIFEST_URL, CONFIG_KHEPRI_OTA_PERIOD_S);
    xTaskCreate(ota_task, "kh_ota", 8192, NULL, 3, &s_task);
}

void kh_ota_check_now(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

void kh_ota_get_status(kh_ota_status_t *out) { *out = s_st; }

void kh_ota_mark_valid(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (p && esp_ota_get_state_partition(p, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "nouvelle version validée");
    }
}
