#include "kh_wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "kh_wifi";

#define NVS_NS "khepri"
#define MAX_RETRY 5

static SemaphoreHandle_t s_mux;
static kh_wifi_status_t s_st;
static char s_pass[65];
static int s_retry;

static TaskHandle_t s_scan_task;
static void scan_task(void *arg);

static void lock(void) { xSemaphoreTake(s_mux, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mux); }

static void creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "wifi_ssid", ssid);
    nvs_set_str(h, "wifi_pass", pass);
    nvs_commit(h);
    nvs_close(h);
}

static bool creds_load(char *ssid, size_t sl, char *pass, size_t pl)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_str(h, "wifi_ssid", ssid, &sl) == ESP_OK && nvs_get_str(h, "wifi_pass", pass, &pl) == ESP_OK;
    nvs_close(h);
    return ok && ssid[0];
}

static void do_connect(void)
{
    wifi_config_t wc = {0};
    lock();
    strlcpy((char *)wc.sta.ssid, s_st.ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, s_pass, sizeof wc.sta.password);
    s_st.state = KH_WIFI_CONNECTING;
    s_st.ip[0] = 0;
    unlock();
    wc.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        lock();
        bool want = s_st.state == KH_WIFI_CONNECTING || s_st.state == KH_WIFI_CONNECTED;
        s_st.ip[0] = 0;
        unlock();
        if (!want) return;
        if (++s_retry <= MAX_RETRY) {
            ESP_LOGW(TAG, "déconnecté, nouvel essai %d/%d", s_retry, MAX_RETRY);
            lock();
            s_st.state = KH_WIFI_CONNECTING;
            unlock();
            esp_wifi_connect();
        } else {
            lock();
            s_st.state = KH_WIFI_FAILED;
            unlock();
            ESP_LOGW(TAG, "échec de connexion à %s", s_st.ssid);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        wifi_ap_record_t ap;
        lock();
        snprintf(s_st.ip, sizeof s_st.ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_st.state = KH_WIFI_CONNECTED;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_st.rssi = ap.rssi;
            s_st.channel = ap.primary;
        }
        unlock();
        s_retry = 0;
        ESP_LOGI(TAG, "connecté à %s, IP %s, canal %u", s_st.ssid, s_st.ip, s_st.channel);
    }
}

void kh_wifi_init(void)
{
    s_mux = xSemaphoreCreateMutex();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    xTaskCreate(scan_task, "kh_scan", 4096, NULL, 3, &s_scan_task);
    char ssid[33] = {0};
    if (creds_load(ssid, sizeof ssid, s_pass, sizeof s_pass)) {
        lock();
        strlcpy(s_st.ssid, ssid, sizeof s_st.ssid);
        s_st.saved = true;
        unlock();
        ESP_LOGI(TAG, "réseau mémorisé : %s", ssid);
        do_connect();
    }
}

/* Balayage bloquant dans une tâche dédiée : plus robuste que l'événement SCAN_DONE, et chaque
 * échec est visible (journal + page WI-FI). Les émissions ESP-NOW qui changent de canal sont
 * suspendues pendant le balayage (kh_wifi_scanning()). */

static void scan_task(void *arg)
{
    static wifi_ap_record_t rec[KH_WIFI_MAX_AP * 2];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        wifi_scan_config_t sc = {
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time = {.active = {.min = 80, .max = 150}},
        };
        esp_err_t e = ESP_FAIL;
        for (int attempt = 0; attempt < 3 && e != ESP_OK; attempt++) {
            if (attempt) {
                esp_wifi_scan_stop();
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            e = esp_wifi_scan_start(&sc, true); /* bloquant, ~2 s */
            if (e == ESP_ERR_WIFI_STATE) {
                /* connexion en cours vers un réseau injoignable : on l'interrompt pour balayer */
                lock();
                bool connected = s_st.state == KH_WIFI_CONNECTED;
                unlock();
                if (!connected) esp_wifi_disconnect();
            }
            if (e != ESP_OK) ESP_LOGW(TAG, "balayage refusé (%s), essai %d/3", esp_err_to_name(e), attempt + 1);
        }
        uint16_t total = 0, n = KH_WIFI_MAX_AP * 2;
        if (e == ESP_OK) {
            esp_wifi_scan_get_ap_num(&total);
            if (esp_wifi_scan_get_ap_records(&n, rec) != ESP_OK) n = 0;
        } else {
            n = 0;
        }
        lock();
        int k = 0;
        for (int i = 0; i < n && k < KH_WIFI_MAX_AP; i++) {
            if (!rec[i].ssid[0]) continue; /* réseaux masqués */
            bool dup = false;
            for (int j = 0; j < k; j++)
                if (!strcmp(s_st.ap[j].ssid, (char *)rec[i].ssid)) dup = true;
            if (dup) continue;
            strlcpy(s_st.ap[k].ssid, (char *)rec[i].ssid, sizeof s_st.ap[k].ssid);
            s_st.ap[k].rssi = rec[i].rssi;
            s_st.ap[k].channel = rec[i].primary;
            s_st.ap[k].secure = rec[i].authmode != WIFI_AUTH_OPEN;
            k++;
        }
        s_st.ap_count = k;
        s_st.scan_err = e;
        s_st.scanning = false;
        s_st.scan_gen++;
        unlock();
        ESP_LOGI(TAG, "balayage : %u point(s) d'accès vu(s), %d affiché(s) (%s)", total, k, esp_err_to_name(e));
        /* la radio reste sur le canal ESP-NOW quand on n'est pas connecté */
        lock();
        bool idle = s_st.state != KH_WIFI_CONNECTED && s_st.state != KH_WIFI_CONNECTING;
        unlock();
        if (idle) esp_wifi_set_channel(CONFIG_KHEPRI_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    }
}

void kh_wifi_scan(void)
{
    lock();
    bool busy = s_st.scanning;
    if (!busy) s_st.scanning = true;
    unlock();
    if (!busy && s_scan_task) xTaskNotifyGive(s_scan_task);
}

bool kh_wifi_scanning(void)
{
    lock();
    bool b = s_st.scanning;
    unlock();
    return b;
}

void kh_wifi_connect(const char *ssid, const char *password)
{
    lock();
    strlcpy(s_st.ssid, ssid, sizeof s_st.ssid);
    strlcpy(s_pass, password ? password : "", sizeof s_pass);
    s_st.saved = true;
    unlock();
    s_retry = 0;
    creds_save(ssid, s_pass);
    ESP_LOGI(TAG, "connexion à %s", ssid);
    do_connect();
}

void kh_wifi_forget(void)
{
    lock();
    s_st.state = KH_WIFI_IDLE;
    s_st.saved = false;
    s_st.ssid[0] = 0;
    s_st.ip[0] = 0;
    s_pass[0] = 0;
    unlock();
    esp_wifi_disconnect();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "wifi_ssid");
        nvs_erase_key(h, "wifi_pass");
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "réseau oublié");
}

void kh_wifi_get_status(kh_wifi_status_t *out)
{
    lock();
    *out = s_st;
    unlock();
    if (out->state == KH_WIFI_CONNECTED) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) out->rssi = ap.rssi;
    }
}

bool kh_wifi_connected(void)
{
    lock();
    bool c = s_st.state == KH_WIFI_CONNECTED;
    unlock();
    return c;
}
