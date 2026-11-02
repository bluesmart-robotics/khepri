#include "kh_ota.h"

#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "espnow.h"
#include "espnow_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kh_peer.h"
#include "kh_wifi.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"

static const char *TAG = "kh_ota";

#define CHECK_PERIOD_MS (CONFIG_KHEPRI_OTA_PERIOD_S * 1000)
#define RETRY_PEER_MS 10000 /* image du rond prête : on repropose toutes les 10 s */
#define MANIFEST_MAX 1024
#define PEER_TRIES 3        /* envois au rond par version et par démarrage */
#define STAGING_SUBTYPE 0x40

static kh_ota_status_t s_st;
static kh_ota_can_update_cb_t s_can;
static TaskHandle_t s_task;

typedef struct {
    char ver[32], url[200];
    uint32_t size;
    uint8_t sha[32];
    bool has_sha;
} manifest_t;

/* ------------------------------------------------------------------ manifeste */
static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static bool fetch_manifest(const char *base, const char *field, manifest_t *m)
{
    memset(m, 0, sizeof *m);
    char *buf = calloc(1, MANIFEST_MAX + 1);
    if (!buf) return false;
    /* paramètre anti-cache : chaque requête porte une URL différente */
    char murl[256];
    snprintf(murl, sizeof murl, "%s?t=%lld", base, (long long)(esp_timer_get_time() / 1000));
    esp_http_client_config_t cfg = {.url = murl, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 8000};
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
            /* un manifeste peut servir plusieurs révisions de carte : url, url_V1, url_V1.1… */
            const cJSON *u = cJSON_GetObjectItem(j, field);
            const cJSON *sz = cJSON_GetObjectItem(j, "size");
            const cJSON *sh = cJSON_GetObjectItem(j, "sha256");
            if (cJSON_IsString(v) && cJSON_IsString(u)) {
                strlcpy(m->ver, v->valuestring, sizeof m->ver);
                strlcpy(m->url, u->valuestring, sizeof m->url);
                if (cJSON_IsNumber(sz)) m->size = (uint32_t)sz->valuedouble;
                if (cJSON_IsString(sh) && strlen(sh->valuestring) == 64) {
                    m->has_sha = true;
                    for (int i = 0; i < 32; i++) {
                        int hi = hexval(sh->valuestring[2 * i]), lo = hexval(sh->valuestring[2 * i + 1]);
                        if (hi < 0 || lo < 0) m->has_sha = false;
                        m->sha[i] = (uint8_t)(hi << 4 | lo);
                    }
                }
                ok = true;
            }
            cJSON_Delete(j);
        } else {
            ESP_LOGW(TAG, "manifeste %s : HTTP %d", base, status);
        }
    }
    if (c) esp_http_client_cleanup(c);
    free(buf);
    return ok;
}

/* ------------------------------------------------------------------ mise à jour de cet écran */
static esp_err_t download_self(const char *url)
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

/* Renvoie true si l'écran redémarre (ne revient pas) */
static void self_cycle(void)
{
    manifest_t m;
    if (!fetch_manifest(CONFIG_KHEPRI_OTA_MANIFEST_URL, CONFIG_KHEPRI_OTA_URL_FIELD, &m)) {
        s_st.state = KH_OTA_CHECK_FAIL;
        return;
    }
    strlcpy(s_st.remote, m.ver, sizeof s_st.remote);
    if (!strcmp(m.ver, s_st.running)) {
        s_st.state = KH_OTA_UP_TO_DATE;
        return;
    }
    if (s_can && !s_can()) {
        s_st.state = KH_OTA_DEFERRED;
        ESP_LOGW(TAG, "version %s disponible, reportée (moteurs en marche ou alerte)", m.ver);
        return;
    }
    ESP_LOGW(TAG, "mise à jour %s → %s depuis %s", s_st.running, m.ver, m.url);
    s_st.state = KH_OTA_DOWNLOADING;
    s_st.progress_pct = 0;
    esp_err_t e = download_self(m.url);
    if (e == ESP_OK) {
        ESP_LOGW(TAG, "mise à jour écrite, redémarrage");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    ESP_LOGE(TAG, "échec de la mise à jour : %s", esp_err_to_name(e));
    s_st.state = KH_OTA_FAILED;
}

/* ------------------------------------------------------------------ mise à jour de l'écran rond */
static kh_ota_peer_status_t s_peer;
static const esp_partition_t *s_stage;
static struct {
    bool ok;
    char ver[32];
    uint32_t size;
    uint8_t sha[32];
    int tries;
} s_staged;

static const esp_partition_t *staging_partition(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, STAGING_SUBTYPE, "fw_rond");
    if (!p) p = esp_ota_get_next_update_partition(NULL); /* table de partitions d'avant : partition OTA libre */
    return p;
}

static bool download_peer(const manifest_t *m)
{
    const esp_partition_t *part = staging_partition();
    if (!part || !m->has_sha || !m->size || m->size > part->size) {
        ESP_LOGE(TAG, "image du rond : partition, taille ou SHA-256 manquants");
        return false;
    }
    s_staged.ok = false;
    s_peer.state = KHP_UPD_DOWNLOADING;
    s_peer.pct = 0;
    ESP_LOGW(TAG, "rond : téléchargement de la version %s (%lu o) vers « %s »", m->ver, (unsigned long)m->size, part->label);
    if (esp_partition_erase_range(part, 0, (m->size + 4095) & ~4095u) != ESP_OK) return false;

    esp_http_client_config_t cfg = {.url = m->url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 15000, .buffer_size = 4096};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    uint8_t *buf = malloc(4096);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    uint32_t got = 0;
    bool ok = c && buf && esp_http_client_open(c, 0) == ESP_OK;
    if (ok) {
        esp_http_client_fetch_headers(c);
        ok = esp_http_client_get_status_code(c) == 200;
    }
    while (ok && got < m->size) {
        int r = esp_http_client_read(c, (char *)buf, 4096);
        if (r <= 0 || got + r > m->size) {
            ok = false;
            break;
        }
        if (esp_partition_write(part, got, buf, r) != ESP_OK) ok = false;
        mbedtls_sha256_update(&sha, buf, r);
        got += r;
        s_peer.pct = (uint8_t)(got * 100ull / m->size);
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (c) esp_http_client_cleanup(c);
    free(buf);
    if (!ok || got != m->size || memcmp(digest, m->sha, 32)) {
        ESP_LOGE(TAG, "rond : téléchargement ou contrôle SHA-256 en échec (%lu/%lu o)", (unsigned long)got, (unsigned long)m->size);
        return false;
    }
    s_stage = part;
    s_staged.ok = true;
    strlcpy(s_staged.ver, m->ver, sizeof s_staged.ver);
    s_staged.size = m->size;
    memcpy(s_staged.sha, m->sha, 32);
    s_staged.tries = 0;
    ESP_LOGI(TAG, "rond : version %s prête à envoyer", m->ver);
    return true;
}

static uint32_t s_sent_max;
static esp_err_t peer_read_cb(size_t off, void *dst, size_t size)
{
    if (off + size > s_sent_max) {
        s_sent_max = off + size;
        s_peer.pct = (uint8_t)(s_sent_max * 100ull / s_staged.size);
    }
    return esp_partition_read(s_stage, off, dst, size);
}

static void send_to_peer(const kh_peer_status_t *p)
{
    if (s_staged.tries >= PEER_TRIES) {
        s_peer.state = KHP_UPD_FAILED;
        return;
    }
    if (s_can && !s_can()) {
        s_peer.state = KHP_UPD_WAITING;
        return;
    }
    khp_offer_t o = {.size = s_staged.size};
    strlcpy(o.ver, s_staged.ver, sizeof o.ver);
    memcpy(o.id, s_staged.sha, 16);
    uint8_t r = kh_peer_offer(&o, 4000);
    if (r != KHP_REFUSE_NONE) {
        s_peer.state = KHP_UPD_WAITING;
        ESP_LOGI(TAG, "rond : mise à jour refusée pour l'instant (raison %u)", r);
        return;
    }
    s_staged.tries++;
    s_peer.state = KHP_UPD_SENDING;
    s_peer.pct = 0;
    s_sent_max = 0;
    ESP_LOGW(TAG, "rond : envoi de la version %s par ESP-NOW (essai %d)", s_staged.ver, s_staged.tries);
    uint8_t addr[1][6];
    memcpy(addr[0], p->mac, 6);
    espnow_ota_result_t res = {0};
    esp_err_t e = espnow_ota_initiator_send(addr, 1, o.id, s_staged.size, peer_read_cb, &res);
    bool ok = e == ESP_OK && res.successed_num == 1;
    espnow_ota_initiator_result_free(&res);
    if (ok) {
        s_peer.pct = 100;
        ESP_LOGI(TAG, "rond : version %s envoyée, il redémarre", s_staged.ver);
    } else {
        s_peer.state = KHP_UPD_FAILED;
        ESP_LOGE(TAG, "rond : envoi en échec (%s)", esp_err_to_name(e));
    }
}

/* Renvoie true s'il reste une image à pousser (on repasse plus vite) */
static bool peer_cycle(void)
{
    kh_peer_status_t p;
    kh_peer_get(&p);
    if (!p.paired || !p.alive) return false;
    if (kh_wifi_connected()) {
        manifest_t m;
        if (fetch_manifest(CONFIG_KHEPRI_OTA_PEER_MANIFEST_URL, "url", &m)) {
            strlcpy(s_peer.remote, m.ver, sizeof s_peer.remote);
            if (!strcmp(m.ver, p.hello.fw)) {
                s_peer.state = KHP_UPD_UP_TO_DATE;
                return false;
            }
            if ((!s_staged.ok || strcmp(s_staged.ver, m.ver)) && !download_peer(&m)) {
                s_peer.state = KHP_UPD_FAILED;
                return false;
            }
        }
    }
    if (!s_staged.ok) return false;
    if (!strcmp(s_staged.ver, p.hello.fw)) {
        s_peer.state = KHP_UPD_UP_TO_DATE;
        return false;
    }
    send_to_peer(&p);
    return s_peer.state == KHP_UPD_WAITING || s_peer.state == KHP_UPD_SENDING;
}

static void ota_task(void *arg)
{
    bool peer_pending = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(peer_pending ? RETRY_PEER_MS : CHECK_PERIOD_MS));
        s_st.last_check_ms = esp_timer_get_time() / 1000;
        if (kh_wifi_connected()) self_cycle();
        else s_st.state = KH_OTA_IDLE;
        if (CONFIG_KHEPRI_OTA_PEER_MANIFEST_URL[0]) peer_pending = peer_cycle();
    }
}

void kh_ota_init(kh_ota_can_update_cb_t can_update)
{
    s_can = can_update;
    strlcpy(s_st.running, esp_app_get_description()->version, sizeof s_st.running);
    const esp_partition_t *p = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "firmware %s sur la partition %s ; manifeste %s toutes les %d s", s_st.running, p ? p->label : "?",
             CONFIG_KHEPRI_OTA_MANIFEST_URL, CONFIG_KHEPRI_OTA_PERIOD_S);
    if (CONFIG_KHEPRI_OTA_PEER_MANIFEST_URL[0]) {
        const esp_partition_t *st = staging_partition();
        ESP_LOGI(TAG, "mises à jour de l'écran rond : %s, image stockée dans « %s »", CONFIG_KHEPRI_OTA_PEER_MANIFEST_URL,
                 st ? st->label : "?");
    }
    if (xTaskCreate(ota_task, "kh_ota", 6144, NULL, 3, &s_task) != pdPASS)
        ESP_LOGE(TAG, "tâche OTA non créée : mémoire insuffisante");
}

void kh_ota_check_now(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

void kh_ota_get_status(kh_ota_status_t *out) { *out = s_st; }
void kh_ota_get_peer_status(kh_ota_peer_status_t *out) { *out = s_peer; }

void kh_ota_mark_valid(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (p && esp_ota_get_state_partition(p, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "nouvelle version validée");
    }
}

/* ------------------------------------------------------------------ rond : réception par ESP-NOW */
static kh_ota_rx_cb_t s_rx_cb;
static volatile bool s_rx_on;
static volatile int64_t s_rx_last;

static void restart_cb(void *arg) { esp_restart(); }

static void rx_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    s_rx_last = esp_timer_get_time() / 1000;
    switch (id) {
    case ESP_EVENT_ESPNOW_OTA_STARTED:
        ESP_LOGW(TAG, "réception d'une mise à jour par ESP-NOW");
        if (s_rx_cb) s_rx_cb(0, false, false);
        break;
    case ESP_EVENT_ESPNOW_OTA_STATUS:
        if (s_rx_cb && data) s_rx_cb((int)*(uint32_t *)data, false, false);
        break;
    case ESP_EVENT_ESPNOW_OTA_FINISH: {
        ESP_LOGW(TAG, "mise à jour reçue et écrite : redémarrage");
        if (s_rx_cb) s_rx_cb(100, true, false);
        static esp_timer_handle_t t;
        const esp_timer_create_args_t a = {.callback = restart_cb, .name = "ota_restart"};
        if (!t && esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, 1500 * 1000);
        break;
    }
    case ESP_EVENT_ESPNOW_OTA_STOPED:
        ESP_LOGE(TAG, "réception de la mise à jour interrompue");
        if (s_rx_cb) s_rx_cb(0, false, true);
        break;
    default: break;
    }
}

void kh_ota_rx_accept(kh_ota_rx_cb_t cb)
{
    s_rx_cb = cb;
    s_rx_last = esp_timer_get_time() / 1000;
    if (s_rx_on) return;
    static bool registered;
    if (!registered) {
        esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, rx_event, NULL);
        registered = true;
    }
    espnow_ota_config_t c = {.skip_version_check = false, .progress_report_interval = 5};
    if (espnow_ota_responder_start(&c) == ESP_OK) s_rx_on = true;
    ESP_LOGI(TAG, "prêt à recevoir une mise à jour de l'écran énergie");
}

void kh_ota_rx_tick(void)
{
    if (s_rx_on && esp_timer_get_time() / 1000 - s_rx_last > 180000) {
        espnow_ota_responder_stop();
        s_rx_on = false;
        ESP_LOGW(TAG, "plus rien reçu depuis 3 min : réception de mise à jour désarmée");
        if (s_rx_cb) s_rx_cb(0, false, true);
    }
}

bool kh_ota_rx_active(void) { return s_rx_on; }
