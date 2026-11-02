#include "kh_peer.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "espnow.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "khepri_link.h"
#include "sdkconfig.h"

static const char *TAG = "kh_peer";

#define TICK_MS 100
#define ALIVE_MS 3000      /* partenaire perdu au-delà */
#define HUNT_DWELL_TICKS 3 /* 300 ms par canal pendant la recherche */
#define HUNT_DELAY_MS 3000 /* on attend un peu avant de quitter le canal de départ */

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_role;
static kh_peer_rx_cb_t s_rx;
static kh_peer_offer_cb_t s_offer_cb;
static kh_peer_status_t s_st;
static int64_t s_last_heard, s_boot;
static volatile bool s_allow_hunt, s_reply;
static khp_hello_t s_mine;
static uint16_t s_seq;

/* proposition reçue (rond) / réponse reçue (énergie) */
static volatile bool s_offer_pending;
static khp_offer_t s_offer;
static SemaphoreHandle_t s_answer_sem;
static khp_answer_t s_answer;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static uint8_t other_role(void) { return s_role == KHP_ROLE_ROND ? KHP_ROLE_ENERGIE : KHP_ROLE_ROND; }

static esp_err_t send_to(const uint8_t *mac, const void *buf, size_t len)
{
    bool bc = mac == NULL;
    if (!bc) espnow_add_peer(mac, NULL);
    espnow_frame_head_t head = {.broadcast = bc, .retransmit_count = 1, .channel = ESPNOW_CHANNEL_CURRENT};
    return espnow_send(ESPNOW_DATA_TYPE_DATA, bc ? ESPNOW_ADDR_BROADCAST : mac, buf, len, &head, pdMS_TO_TICKS(20));
}

static void send_hello(bool broadcast)
{
    khp_hello_t h;
    portENTER_CRITICAL(&s_lock);
    h = s_mine;
    h.role = s_role;
    if (s_st.paired) h.flags |= KHP_F_PAIRED;
    if (!s_st.alive) h.flags |= KHP_F_SEEK;
    uint8_t mac[6];
    memcpy(mac, s_st.mac, 6);
    portEXIT_CRITICAL(&s_lock);
    uint8_t buf[96];
    size_t len = khp_pack_hello(buf, sizeof buf, &h);
    if (len) send_to(broadcast ? NULL : mac, buf, len);
}

/* ------------------------------------------------------------------ réception (tâche esp-now) */
static void on_other(const uint8_t *src, const uint8_t *data, size_t size, int8_t rssi)
{
    int t = khp_type(data, size);
    if (t == 0) { /* télémétrie KHN relayée par le partenaire */
        khn_hdr_t h;
        khn_frame_t f[KHN_MAX_FRAMES];
        if (!s_st.paired || memcmp(src, s_st.mac, 6) != 0) return;
        int n = khn_unpack(data, size, &h, f, KHN_MAX_FRAMES);
        if (n >= 0 && s_rx) s_rx(&h, f, n, rssi);
        return;
    }
    if (t == KHP_T_HELLO) {
        khp_hello_t h;
        if (!khp_unpack_hello(data, size, &h) || h.role != other_role()) return;
        bool paired_now = false;
        portENTER_CRITICAL(&s_lock);
        if (!s_st.paired && rssi >= KHP_PAIR_RSSI) {
            memcpy(s_st.mac, src, 6);
            s_st.paired = paired_now = true;
        }
        if (s_st.paired && !memcmp(src, s_st.mac, 6)) {
            s_st.hello = h;
            s_st.rssi = rssi;
            s_st.alive = true;
            s_last_heard = now_ms();
            if (h.flags & KHP_F_SEEK) s_reply = true; /* il nous cherche : répondre tout de suite */
        }
        portEXIT_CRITICAL(&s_lock);
        if (paired_now)
            ESP_LOGI(TAG, "écran %s trouvé : " MACSTR " (%d dBm, v%s)", h.role == KHP_ROLE_ROND ? "rond" : "énergie",
                     MAC2STR(src), rssi, h.fw);
        return;
    }
    if (!s_st.paired || memcmp(src, s_st.mac, 6) != 0) return; /* le reste ne vient que du partenaire */
    if (t == KHP_T_OFFER && s_role == KHP_ROLE_ROND) {
        khp_offer_t o;
        if (!khp_unpack_offer(data, size, &o)) return;
        portENTER_CRITICAL(&s_lock);
        s_offer = o;
        s_offer_pending = true;
        portEXIT_CRITICAL(&s_lock);
    } else if (t == KHP_T_ANSWER && s_role == KHP_ROLE_ENERGIE) {
        khp_answer_t a;
        if (!khp_unpack_answer(data, size, &a)) return;
        s_answer = a;
        if (s_answer_sem) xSemaphoreGive(s_answer_sem);
    }
}

/* ------------------------------------------------------------------ tâche 10 Hz */
static void peer_task(void *arg)
{
    int tick = 0, dwell = 0;
    bool was_alive = false, was_hunting = false;
    uint8_t hunt_ch = CONFIG_KHEPRI_ESPNOW_CHANNEL;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        tick++;
        int64_t t = now_ms();
        uint8_t ch = 0;
        wifi_second_chan_t sc;
        esp_wifi_get_channel(&ch, &sc);

        portENTER_CRITICAL(&s_lock);
        if (s_st.alive && t - s_last_heard > ALIVE_MS) s_st.alive = false;
        bool alive = s_st.alive, paired = s_st.paired;
        s_st.channel = ch;
        portEXIT_CRITICAL(&s_lock);
        if (was_alive && !alive) ESP_LOGW(TAG, "l'autre écran ne répond plus");
        was_alive = alive;

        /* rond sans partenaire ni maître : parcours des canaux (l'écran énergie a pu suivre son Wi-Fi) */
        bool hunt = s_role == KHP_ROLE_ROND && s_allow_hunt && !alive && t - s_boot > HUNT_DELAY_MS;
        s_st.hunting = hunt;
        if (hunt) {
            if (++dwell >= HUNT_DWELL_TICKS) {
                dwell = 0;
                hunt_ch = hunt_ch >= 13 ? 1 : hunt_ch + 1;
                if (esp_wifi_set_channel(hunt_ch, WIFI_SECOND_CHAN_NONE) == ESP_OK) send_hello(true);
            }
        } else {
            /* fin de recherche sans avoir retrouvé l'autre écran (LIER, maître revenu) : retour au canal
             * de la liaison avec le maître */
            if (was_hunting && !alive && ch != CONFIG_KHEPRI_ESPNOW_CHANNEL) {
                esp_wifi_set_channel(CONFIG_KHEPRI_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
                ch = CONFIG_KHEPRI_ESPNOW_CHANNEL;
            }
            dwell = 0;
            hunt_ch = ch ? ch : CONFIG_KHEPRI_ESPNOW_CHANNEL;
            if (s_reply) {
                s_reply = false;
                send_hello(false);
            } else if (!alive) {
                if (tick % 5 == 0) send_hello(true); /* 2 Hz : on se fait connaître */
            } else if (tick % 10 == 0) {
                send_hello(false); /* 1 Hz : battement de cœur */
            }
        }
        was_hunting = hunt;
        if (hunt && s_reply) {
            s_reply = false;
            send_hello(false);
        }

        /* rond : réponse à une proposition de mise à jour */
        if (s_offer_pending) {
            khp_offer_t o;
            portENTER_CRITICAL(&s_lock);
            o = s_offer;
            s_offer_pending = false;
            portEXIT_CRITICAL(&s_lock);
            khp_answer_t a = {.accept = 0, .reason = KHP_REFUSE_BUSY};
            if (s_offer_cb) a.reason = s_offer_cb(&o);
            a.accept = a.reason == KHP_REFUSE_NONE;
            uint8_t buf[16];
            size_t len = khp_pack_answer(buf, sizeof buf, &a);
            if (len) send_to(s_st.mac, buf, len);
        }
    }
}

/* ------------------------------------------------------------------ API */
void kh_peer_init(uint8_t role, kh_peer_rx_cb_t rx)
{
    s_role = role;
    s_rx = rx;
    s_boot = now_ms();
    s_answer_sem = xSemaphoreCreateBinary();
    strlcpy(s_mine.fw, esp_app_get_description()->version, sizeof s_mine.fw);
    khepri_link_set_other_cb(on_other);
    if (xTaskCreate(peer_task, "kh_peer", 5120, NULL, 3, NULL) != pdPASS) ESP_LOGE(TAG, "tâche non créée");
    ESP_LOGI(TAG, "écran %s : recherche de l'autre écran", role == KHP_ROLE_ROND ? "rond" : "énergie");
}

void kh_peer_allow_hunt(bool allow) { s_allow_hunt = allow; }

void kh_peer_set_hello(uint8_t flags, uint8_t upd_state, uint8_t upd_pct, const char *upd_ver, const char *ssid)
{
    portENTER_CRITICAL(&s_lock);
    s_mine.flags = flags;
    s_mine.upd_state = upd_state;
    s_mine.upd_pct = upd_pct;
    strlcpy(s_mine.upd_ver, upd_ver ? upd_ver : "", sizeof s_mine.upd_ver);
    strlcpy(s_mine.ssid, ssid ? ssid : "", sizeof s_mine.ssid);
    portEXIT_CRITICAL(&s_lock);
}

void kh_peer_send_frames(const khn_frame_t *f, int n, uint8_t flags)
{
    if (!s_st.paired || !s_st.alive) return;
    uint8_t buf[KHN_PAYLOAD_MAX];
    size_t len = khn_pack(buf, sizeof buf, s_seq++, flags, f, (size_t)n);
    if (len) send_to(s_st.mac, buf, len);
}

void kh_peer_get(kh_peer_status_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_st;
    portEXIT_CRITICAL(&s_lock);
}

bool kh_peer_alive(void) { return s_st.alive; }

void kh_peer_repair(void)
{
    portENTER_CRITICAL(&s_lock);
    s_st.paired = s_st.alive = false;
    memset(s_st.mac, 0, 6);
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "partenaire oublié, nouvelle recherche");
}

void kh_peer_on_offer(kh_peer_offer_cb_t cb) { s_offer_cb = cb; }

uint8_t kh_peer_offer(const khp_offer_t *o, int timeout_ms)
{
    if (!s_st.paired || !s_st.alive) return KHP_REFUSE_BUSY;
    uint8_t buf[64];
    size_t len = khp_pack_offer(buf, sizeof buf, o);
    if (!len) return KHP_REFUSE_BUSY;
    xSemaphoreTake(s_answer_sem, 0); /* oublie une vieille réponse */
    for (int waited = 0; waited < timeout_ms; waited += 500) {
        send_to(s_st.mac, buf, len);
        if (xSemaphoreTake(s_answer_sem, pdMS_TO_TICKS(500)) == pdTRUE)
            return s_answer.accept ? KHP_REFUSE_NONE : (s_answer.reason ? s_answer.reason : KHP_REFUSE_BUSY);
    }
    return KHP_REFUSE_BUSY;
}
