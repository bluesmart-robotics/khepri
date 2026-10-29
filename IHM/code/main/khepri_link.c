#include "khepri_link.h"
#include "kh_wifi.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_ctrl.h"
#include "espnow_storage.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "khepri_link";

#define BIND_WINDOW_US (15LL * 1000000LL)
#define NVS_NS "khepri"
#define NVS_KEY_MAC "master_mac"

static khepri_link_rx_cb_t s_cb;
static uint8_t s_master[6];
static volatile bool s_bound, s_binding;
static int64_t s_bind_deadline;
static uint16_t s_hb;

/* ------------------------------------------------------------------ NVS */
static void mac_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof s_master;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, NVS_KEY_MAC, s_master, &len) == ESP_OK && len == 6) s_bound = true;
    nvs_close(h);
}

static void mac_save(bool keep)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (keep) nvs_set_blob(h, NVS_KEY_MAC, s_master, 6);
    else nvs_erase_key(h, NVS_KEY_MAC);
    nvs_commit(h);
    nvs_close(h);
}

/* ------------------------------------------------------------------ Wi-Fi (comme app_wifi_init de la télécommande BSR) */
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_KHEPRI_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    esp_wifi_set_max_tx_power(60); /* 15 dBm : assez pour un cockpit, appels de courant réduits */
}

/* ------------------------------------------------------------------ réception de la télémétrie */
static esp_err_t on_data(uint8_t *src, void *data, size_t size, wifi_pkt_rx_ctrl_t *rx)
{
    khn_hdr_t h;
    khn_frame_t f[KHN_MAX_FRAMES];
    int n = khn_unpack(data, size, &h, f, KHN_MAX_FRAMES);
    if (n < 0) return ESP_OK; /* pas un paquet KHÉPRI */

    if (s_binding && esp_timer_get_time() < s_bind_deadline) {
        /* premier maître entendu après LIER : c'est le nôtre */
        memcpy(s_master, src, 6);
        s_bound = true;
        s_binding = false;
        mac_save(true);
        ESP_LOGI(TAG, "appairé au maître " MACSTR, MAC2STR(s_master));
    }
    if (!s_bound || memcmp(src, s_master, 6) != 0) return ESP_OK;
    if (s_cb) s_cb(&h, f, n, rx ? (int8_t)rx->rssi : -127);
    return ESP_OK;
}

void khepri_link_init(khepri_link_rx_cb_t cb)
{
    s_cb = cb;
    mac_load();
    espnow_storage_init();
    wifi_init();
    espnow_config_t ec = ESPNOW_INIT_CONFIG_DEFAULT();
    ec.qsize = 16;
    espnow_init(&ec);
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, on_data);
    if (s_bound) ESP_LOGI(TAG, "maître mémorisé " MACSTR, MAC2STR(s_master));
    else ESP_LOGI(TAG, "aucun maître : page SYSTÈME → LIER");
}

void khepri_link_bind(bool bind)
{
    if (bind) {
        ESP_LOGI(TAG, "LIER : recherche d'un maître pendant 15 s");
        espnow_ctrl_initiator_bind(KHN_ATTR_INITIATOR, true);
        s_bound = false;
        s_binding = true;
        s_bind_deadline = esp_timer_get_time() + BIND_WINDOW_US;
    } else {
        ESP_LOGI(TAG, "DÉLIER");
        espnow_ctrl_initiator_bind(KHN_ATTR_INITIATOR, false);
        s_binding = false;
        s_bound = false;
        memset(s_master, 0, sizeof s_master);
        mac_save(false);
    }
}

bool khepri_link_bound(void) { return s_bound; }
bool khepri_link_binding(void) { return s_binding; }
void khepri_link_master_mac(uint8_t mac[6]) { memcpy(mac, s_master, 6); }

bool khepri_link_send_cmd(uint8_t code, uint32_t arg)
{
    if (!s_bound) return false;
    uint32_t v = khn_cmd(code, arg);
    esp_err_t e = espnow_ctrl_initiator_send(KHN_ATTR_INITIATOR, KHN_ATTR_RESPONDER, v);
    if (code != KHN_CMD_HEARTBEAT) ESP_LOGI(TAG, "commande 0x%08lx → %s", (unsigned long)v, esp_err_to_name(e));
    return e == ESP_OK;
}

void khepri_link_tick(void)
{
    if (s_binding && esp_timer_get_time() >= s_bind_deadline) {
        s_binding = false;
        ESP_LOGW(TAG, "aucun maître n'a répondu à LIER");
    }
    /* l'émission de commande parcourt les canaux : pas pendant un balayage Wi-Fi */
    if (s_bound && !kh_wifi_scanning()) khepri_link_send_cmd(KHN_CMD_HEARTBEAT, s_hb++);
}
