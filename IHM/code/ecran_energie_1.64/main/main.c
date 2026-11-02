/*
 * main.c — KHÉPRI : écran ÉNERGIE sur Waveshare ESP32-S3-Touch-AMOLED-1.64 (280×456, portrait).
 *
 * Même ossature que l'écran rond 1.75 :
 *   PARTIE 1  écran AMOLED en QSPI (pilote esp_lcd_sh8601, commandes de bsr_screen pour cette dalle)
 *   PARTIE 2  LVGL 8.3 : une seule tâche touche LVGL, tactile FT3168
 *   PARTIE 3  liaison ESP-NOW automatique avec l'écran rond, qui lui relaie la télémétrie
 *   PARTIE 4  Wi-Fi (seul cet écran s'y connecte) : ses mises à jour, et celles du rond, qu'il lui
 *             pousse par ESP-NOW
 * L'écran est passif : aucune commande moteur ne part d'ici.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/spi_master.h"
#include "energie_screen.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_bsp.h"
#include "kh_model.h"
#include "kh_ota.h"
#include "kh_wifi.h"
#include "kh_peer.h"
#include "khepri_link.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "khepri_energie";

/* ── Brochage Waveshare ESP32-S3-Touch-AMOLED-1.64 (seul CS change entre V1 et V1.1) ── */
#define LCD_HOST SPI2_HOST
#define PIN_LCD_CS CONFIG_KHEPRI_LCD_CS
#define PIN_LCD_PCLK 10
#define PIN_LCD_D0 11
#define PIN_LCD_D1 12
#define PIN_LCD_D2 13
#define PIN_LCD_D3 14
#define PIN_LCD_RST 21 /* écran ET tactile */
#define LCD_W KE_SCR_W
#define LCD_H KE_SCR_H
#define LCD_X_GAP 20   /* la dalle 280 px est centrée dans la mémoire du contrôleur */
#define TOUCH_ADDR 0x38

#define LVGL_BUF_LINES 48 /* 2 × 280 × 48 × 2 o ≈ 54 Ko de DMA interne */
#define LVGL_TICK_MS 2

/* Commandes d'initialisation de la dalle 1.64 (reprises de bsr_screen) */
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 80},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 1},
    {0x63, (uint8_t[]){0xFF}, 1, 1},
    {0x51, (uint8_t[]){0x00}, 1, 1},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static SemaphoreHandle_t s_model_mux;
static esp_lcd_panel_io_handle_t s_io;
static kh_model_t s_model;
static ke_local_t s_local;

/* LVGL n'est pas réentrant : seule lvgl_task l'appelle ; ui_task lui dépose un instantané */
static SemaphoreHandle_t s_snap_mux;
static kh_model_t s_snap_model;
static ke_local_t s_snap_local;
static volatile bool s_snap_new;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ══════════════════════════════════════════════
 *  LVGL
 * ══════════════════════════════════════════════ */
static bool notify_flush_ready(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t *)ctx);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, map);
}

/* Le contrôleur exige des zones de largeur et de hauteur paires */
static void lvgl_rounder_cb(lv_disp_drv_t *drv, lv_area_t *a)
{
    a->x1 = (a->x1 >> 1) << 1;
    a->y1 = (a->y1 >> 1) << 1;
    a->x2 = ((a->x2 >> 1) << 1) + 1;
    a->y2 = ((a->y2 >> 1) << 1) + 1;
}

/* Tactile FT3168 : registre 0x02 = nombre de points, 0x03-0x06 = X puis Y (12 bits) */
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t n = 0, b[4];
    data->state = LV_INDEV_STATE_RELEASED;
    if (I2C_read_buff(TOUCH_ADDR, 0x02, &n, 1) != 0 || !n) return;
    if (I2C_read_buff(TOUCH_ADDR, 0x03, b, 4) != 0) return;
    int x = ((b[0] & 0x0F) << 8) | b[1];
    int y = ((b[2] & 0x0F) << 8) | b[3];
    if (x == 0 && y == 0) return; /* point fantôme connu du FT3168 */
    data->point.x = x < LCD_W ? x : LCD_W - 1;
    data->point.y = y < LCD_H ? y : LCD_H - 1;
    data->state = LV_INDEV_STATE_PRESSED;
}

static void touch_init(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 5; i++) { /* le FT3168 met un instant à répondre après le reset */
        if (I2C_writr_buff(TOUCH_ADDR, 0x00, &v, 1) == 0) return;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGE(TAG, "tactile FT3168 muet");
}

static void lvgl_tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_MS); }

static void lvgl_task(void *arg)
{
    static kh_model_t model;
    static ke_local_t local;
    static bool have;
    int64_t last = 0;
    for (;;) {
        int64_t t = now_ms();
        bool fresh = false;
        if (s_snap_new && xSemaphoreTake(s_snap_mux, 0) == pdTRUE) {
            if (s_snap_new) {
                model = s_snap_model;
                local = s_snap_local;
                s_snap_new = false;
                have = fresh = true;
            }
            xSemaphoreGive(s_snap_mux);
        }
        if (have && (fresh || t - last >= 250)) {
            last = t;
            ke_screen_update(&model, &local, t);
        }
        uint32_t delay = lv_timer_handler();
        if (delay > 10) delay = 10;
        if (delay < 1) delay = 1;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

/* Luminosité : commande 0x51 du contrôleur AMOLED */
static void set_brightness(int pct)
{
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    uint8_t v = (uint8_t)(pct * 255 / 100);
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (0x51 << 8), &v, 1);
    s_local.brightness_pct = pct;
}

/* ══════════════════════════════════════════════
 *  Réglages persistants
 * ══════════════════════════════════════════════ */
static void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("khepri", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "sim", s_local.demo);
    nvs_set_u8(h, "lum", (uint8_t)s_local.brightness_pct);
    nvs_commit(h);
    nvs_close(h);
}

static void settings_load(void)
{
#ifdef CONFIG_KHEPRI_DEMO_DEFAULT
    s_local.demo = true;
#else
    s_local.demo = false; /* normalement : on affiche ce que relaie l'écran rond */
#endif
    s_local.brightness_pct = 100;
    nvs_handle_t h;
    if (nvs_open("khepri", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, "sim", &v) == ESP_OK) s_local.demo = v;
    if (nvs_get_u8(h, "lum", &v) == ESP_OK) s_local.brightness_pct = v;
    nvs_close(h);
}

/* ══════════════════════════════════════════════
 *  Rappels écran → radio (dans la tâche LVGL)
 * ══════════════════════════════════════════════ */
static void model_reset(void)
{
    kh_model_init(&s_model);
    s_model.odrv_node[0] = CONFIG_KHEPRI_ODRV_NODE_M0;
    s_model.odrv_node[1] = CONFIG_KHEPRI_ODRV_NODE_M1;
}

static void ui_on_repair(void) { kh_peer_repair(); }

static void ui_on_demo(bool en)
{
    xSemaphoreTake(s_model_mux, portMAX_DELAY);
    s_local.demo = en;
    model_reset();
    xSemaphoreGive(s_model_mux);
    settings_save();
}

static void ui_on_brightness(int pct)
{
    set_brightness(pct);
    settings_save();
}

static void ui_on_wifi_scan(void) { kh_wifi_scan(); }
static void ui_on_wifi_connect(const char *ssid, const char *pass) { kh_wifi_connect(ssid, pass); }
static void ui_on_wifi_forget(void) { kh_wifi_forget(); }
static void ui_on_ota_check(void) { kh_ota_check_now(); }

/* Mise à jour autorisée moteurs arrêtés (ou régime inconnu) : pas de redémarrage en vol motorisé */
static bool ota_can_update(void)
{
    bool ok = true;
    int64_t t = now_ms();
    xSemaphoreTake(s_model_mux, portMAX_DELAY);
    float w;
    if (!s_local.demo && kh_power_total(&s_model, t, &w) && w > 100.0f) ok = false;
    for (int k = 0; k < 2; k++)
        if (!s_local.demo && kh_fresh(s_model.t_motor[k], t, KH_STALE_MS) && fabsf(s_model.rpm[k]) > 30.0f) ok = false;
    xSemaphoreGive(s_model_mux);
    return ok;
}

/* ══════════════════════════════════════════════
 *  Radio → modèle
 * ══════════════════════════════════════════════ */
/* Télémétrie relayée par l'écran rond (celle du maître, ou sa simulation) */
static void on_rx(const khn_hdr_t *h, const khn_frame_t *f, int n, int8_t rssi)
{
    if (s_local.demo) return; /* la simulation locale a la main */
    xSemaphoreTake(s_model_mux, portMAX_DELAY);
    kh_model_apply_packet(&s_model, h, f, n, rssi, now_ms());
    xSemaphoreGive(s_model_mux);
}

/* Simulation autonome : un pilote fictif alterne moteur arrêté, suivi du soleil, croisière et
 * montée, pour voir la flèche du bilan changer de sens. Cycle de 2 minutes. */
static float demo_throttle(int64_t t)
{
    int s = (int)((t / 1000) % 120);
    return s < 30 ? 0.0f : s < 60 ? 22.0f : s < 95 ? 70.0f : 105.0f;
}

/* ══════════════════════════════════════════════
 *  Tâches
 * ══════════════════════════════════════════════ */
/* 10 Hz : simulation éventuelle, puis instantané pour l'écran */
static void ui_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint16_t seq = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
        int64_t t = now_ms();
        xSemaphoreTake(s_model_mux, portMAX_DELAY);
        if (s_local.demo) {
            kh_demo_set_throttle(demo_throttle(t));
            khn_frame_t fr[KHN_MAX_FRAMES];
            int n = kh_demo_frames(t, fr, KHN_MAX_FRAMES);
            khn_hdr_t h = {.version = KHN_VERSION, .type = KHN_T_CAN_BATCH, .seq = seq++, .count = (uint8_t)n,
                           .flags = KHN_HF_DEMO};
            kh_model_apply_packet(&s_model, &h, fr, n, -40, t);
        }
        kh_model_t snap = s_model;
        xSemaphoreGive(s_model_mux);

        kh_wifi_get_status(&s_local.wifi);
        kh_ota_get_status(&s_local.ota);
        kh_ota_get_peer_status(&s_local.rond);
        kh_peer_get(&s_local.peer);
        bool wifi = s_local.wifi.state == KH_WIFI_CONNECTED;
        kh_peer_set_hello((wifi ? KHP_F_WIFI : 0) | (s_local.demo ? KHP_F_DEMO : 0), s_local.rond.state, s_local.rond.pct,
                          s_local.rond.remote, wifi ? s_local.wifi.ssid : "");
        xSemaphoreTake(s_snap_mux, portMAX_DELAY);
        s_snap_model = snap;
        s_snap_local = s_local;
        s_snap_new = true;
        xSemaphoreGive(s_snap_mux);
    }
}

/* 1 Hz : appairage, battement de cœur vers le maître */
static void link_task(void *arg)
{
    int t = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        khepri_link_tick();
        if (++t == 20) kh_ota_mark_valid(); /* 20 s sans plantage : la nouvelle version est gardée */
    }
}

static void start_task(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio, BaseType_t core,
                       bool psram_stack)
{
    BaseType_t ok;
    if (psram_stack)
        ok = xTaskCreatePinnedToCoreWithCaps(fn, name, stack, NULL, prio, NULL, core, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    else
        ok = xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, NULL, core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "IMPOSSIBLE de créer la tâche %s (%lu o)", name, (unsigned long)stack);
        abort();
    }
}

/* ══════════════════════════════════════════════
 *  APP_MAIN
 * ══════════════════════════════════════════════ */
void app_main(void)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    settings_load();
    ESP_LOGI(TAG, "carte 1.64 %s, écran sur GPIO%d", CONFIG_KHEPRI_BOARD_REV, PIN_LCD_CS);

    /* ── PARTIE 1 : écran AMOLED en QSPI ── */
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, notify_flush_ready, &disp_drv);
    io_config.pclk_hz = 40 * 1000 * 1000;
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io));
    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(s_io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, LCD_X_GAP, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    I2C_master_Init();
    touch_init();

    /* ── PARTIE 2 : LVGL ── */
    lv_init();
    const size_t buf_px = LCD_W * LVGL_BUF_LINES;
    lv_color_t *buf1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 && buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf_px);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_W;
    disp_drv.ver_res = LCD_H;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.rounder_cb = lvgl_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    const esp_timer_create_args_t tick_args = {.callback = lvgl_tick_cb, .name = "lvgl_tick"};
    esp_timer_handle_t tick;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick, LVGL_TICK_MS * 1000));

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    s_snap_mux = xSemaphoreCreateMutex();
    s_model_mux = xSemaphoreCreateMutex();
    model_reset();

    s_local.espnow_channel = CONFIG_KHEPRI_ESPNOW_CHANNEL;
    s_local.thrust_ref_w = CONFIG_KHEPRI_THRUST_REF_W;
    s_local.batt_wh = CONFIG_KHEPRI_BATT_WH;
    s_local.pv_peak_w = CONFIG_KHEPRI_PV_PEAK_W;
    s_local.level_w = CONFIG_KHEPRI_LEVEL_POWER_W;
    strlcpy(s_local.board, CONFIG_KHEPRI_BOARD_REV, sizeof s_local.board);

    const ke_cb_t cb = {.on_repair = ui_on_repair,
                        .on_demo = ui_on_demo,
                        .on_brightness = ui_on_brightness,
                        .on_wifi_scan = ui_on_wifi_scan,
                        .on_wifi_connect = ui_on_wifi_connect,
                        .on_wifi_forget = ui_on_wifi_forget,
                        .on_ota_check = ui_on_ota_check};
    ke_screen_init(&cb);
    set_brightness(s_local.brightness_pct);
    start_task(lvgl_task, "lvgl", 12 * 1024, 5, 1, false);

    /* ── PARTIE 3 : radio, liaison avec l'écran rond ── */
    khepri_link_init(NULL); /* pas de maître direct : tout passe par l'écran rond */
    if (khepri_link_bound()) khepri_link_bind(false); /* oublie une liaison directe d'une version précédente */
    kh_peer_init(KHP_ROLE_ENERGIE, on_rx);
    /* ── PARTIE 4 : Wi-Fi et mises à jour ── */
    kh_wifi_init();
    kh_ota_init(ota_can_update);
    start_task(ui_task, "ui", 10 * 1024, 4, tskNO_AFFINITY, true);
    start_task(link_task, "link", 6 * 1024, 2, tskNO_AFFINITY, false); /* écrit l'otadata : pile interne */

    ESP_LOGI(TAG, "mémoire libre : interne %u o, PSRAM %u o", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "=== KHÉPRI écran énergie prêt (simulation %s) ===", s_local.demo ? "ACTIVE" : "inactive");
}
