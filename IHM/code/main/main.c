/*
 * main.c — KHÉPRI : écran pilote sur Waveshare ESP32-S3-Touch-AMOLED-1.75.
 *
 * Même ossature que bsr_screen (télécommande BSR, AMOLED 1,64") :
 *   PARTIE 1  écran AMOLED en QSPI (pilote esp_lcd_sh8601, compatible CO5300 avec ses commandes)
 *   PARTIE 2  LVGL 8.3 : tampons DMA, tâche LVGL protégée par mutex, tactile
 *   PARTIE 3  liaison ESP-NOW vers le maître, molette (accéléromètre), bouton PWR, simulation
 *   PARTIE 4  Wi-Fi (page WI-FI) et mises à jour OTA depuis GitHub
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "i2c_bsp.h"
#include "kh_board.h"
#include "kh_model.h"
#include "khepri_link.h"
#include "khepri_screen.h"
#include "kh_ota.h"
#include "kh_wifi.h"
#include "knob.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "qmi8658.h"
#include "sdkconfig.h"

static const char *TAG = "khepri_main";

#define LCD_HOST SPI2_HOST
#define LVGL_BUF_LINES 36 /* 2 × 466 × 36 × 2 o ≈ 67 Ko de DMA interne */
#define LVGL_TICK_MS 2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1

/* ══════════════════════════════════════════════
 *  Commandes d'initialisation du CO5300 (BSP Waveshare 1.75)
 * ══════════════════════════════════════════════ */
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static SemaphoreHandle_t s_model_mux;
static esp_lcd_panel_io_handle_t s_io;

static kh_model_t s_model;
static kh_local_t s_local;
static knob_t s_knob;

/* ══════════════════════════════════════════════
 *  LVGL (même schéma que la télécommande BSR)
 * ══════════════════════════════════════════════ */
/* LVGL n'est pas réentrant : une seule tâche, lvgl_task, l'appelle. Les autres tâches lui
 * déposent un instantané des données (s_snap_*) et les crans de molette / clics (s_in_*). */
static SemaphoreHandle_t s_snap_mux;
static kh_model_t s_snap_model;
static kh_local_t s_snap_local;
static bool s_snap_new;
static portMUX_TYPE s_in_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_in_pushes, s_in_clicks;
/* Consigne de régime réglée au roulis [%] ; le roulis n'agit que sur la page principale, hors alerte */
static float s_throttle;
static int64_t s_throttle_ms;
static volatile bool s_roll_enabled = true;
#define THROTTLE_MAX_PCT 122.0f

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

/* Le CO5300 exige des zones de largeur et de hauteur paires */
static void lvgl_rounder_cb(lv_disp_drv_t *drv, lv_area_t *a)
{
    a->x1 = (a->x1 >> 1) << 1;
    a->y1 = (a->y1 >> 1) << 1;
    a->x2 = ((a->x2 >> 1) << 1) + 1;
    a->y2 = ((a->y2 >> 1) << 1) + 1;
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (kh_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_MS); }

static void lvgl_task(void *arg)
{
    static kh_model_t model;
    static kh_local_t local;
    static bool have;
    int64_t last = 0;
    for (;;) {
        portENTER_CRITICAL(&s_in_lock);
        int pushes = s_in_pushes, clicks = s_in_clicks;
        s_in_pushes = s_in_clicks = 0;
        portEXIT_CRITICAL(&s_in_lock);
        if (pushes) kh_screen_push();
        if (clicks) kh_screen_button();
        s_roll_enabled = kh_screen_page() == KH_PAGE_MOTORS && !kh_screen_alarm_visible();

        int64_t t = esp_timer_get_time() / 1000;
        if (t - last >= 100) {
            last = t;
            if (xSemaphoreTake(s_snap_mux, 0) == pdTRUE) { /* jamais bloquant */
                if (s_snap_new) {
                    model = s_snap_model;
                    local = s_snap_local;
                    s_snap_new = false;
                    have = true;
                }
                xSemaphoreGive(s_snap_mux);
            }
            if (have) {
                kh_knob_view_t knob = {0};
                kh_screen_update(&model, &local, &knob, t);
            }
        }

        uint32_t delay = lv_timer_handler();
        if (delay > 20) delay = 20; /* revenir assez souvent pour les mises à jour et la molette */
        if (delay < LVGL_TASK_MIN_DELAY_MS) delay = LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

/* Luminosité : commande 0x51 du contrôleur AMOLED (pas de rétroéclairage PWM) */
static void set_brightness(int pct)
{
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    uint8_t v = (uint8_t)(pct * 255 / 100);
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (0x51 << 8), &v, 1);
    s_local.brightness_pct = pct;
}

/* ══════════════════════════════════════════════
 *  Diagnostic : cause du dernier redémarrage, compteur de redémarrages anormaux
 * ══════════════════════════════════════════════ */
static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "mise sous tension";
    case ESP_RST_SW: return "logiciel";
    case ESP_RST_PANIC: return "PLANTAGE";
    case ESP_RST_INT_WDT: return "CHIEN DE GARDE INT";
    case ESP_RST_TASK_WDT: return "CHIEN DE GARDE TACHE";
    case ESP_RST_WDT: return "CHIEN DE GARDE";
    case ESP_RST_BROWNOUT: return "BAISSE DE TENSION";
    case ESP_RST_DEEPSLEEP: return "réveil";
    case ESP_RST_USB: return "USB";
    case ESP_RST_EXT: return "bouton reset";
    default: return "autre";
    }
}

static void boot_diag(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    bool bad = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
               r == ESP_RST_BROWNOUT;
    uint32_t n = 0;
    nvs_handle_t h;
    if (nvs_open("khepri", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "bad_boots", &n);
        if (bad) {
            n++;
            nvs_set_u32(h, "bad_boots", n);
            nvs_commit(h);
        }
        nvs_close(h);
    }
    snprintf(s_local.reset_text, sizeof s_local.reset_text, "%s · %lu anomalie(s)", reset_name(r),
             (unsigned long)n);
    if (bad) ESP_LOGE(TAG, "redémarrage anormal : %s (%lu au total)", reset_name(r), (unsigned long)n);
    else ESP_LOGI(TAG, "démarrage : %s", reset_name(r));
}

/* ══════════════════════════════════════════════
 *  Réglages persistants
 * ══════════════════════════════════════════════ */
static void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("khepri", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "demo", s_local.demo);
    nvs_set_u8(h, "lum", (uint8_t)s_local.brightness_pct);
    nvs_commit(h);
    nvs_close(h);
}

static void settings_load(void)
{
    s_local.demo = CONFIG_KHEPRI_DEMO_DEFAULT;
    s_local.brightness_pct = 100;
    nvs_handle_t h;
    if (nvs_open("khepri", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, "demo", &v) == ESP_OK) s_local.demo = v;
    if (nvs_get_u8(h, "lum", &v) == ESP_OK) s_local.brightness_pct = v;
    nvs_close(h);
}

/* ══════════════════════════════════════════════
 *  Rappels écran → radio (appelés dans la tâche LVGL, verrou déjà pris)
 * ══════════════════════════════════════════════ */
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void ui_on_bind(bool bind) { khepri_link_bind(bind); }

static void ui_on_mark(void)
{
    static uint16_t n;
    bool ok = khepri_link_send_cmd(KHN_CMD_MARK, ++n);
    ESP_LOGI(TAG, "REPÈRE %u %s", n, ok ? "envoyé" : "non envoyé (pas de maître)");
}

static void ui_on_demo(bool en)
{
    xSemaphoreTake(s_model_mux, portMAX_DELAY);
    s_local.demo = en;
    kh_model_init(&s_model);
    s_model.odrv_node[0] = CONFIG_KHEPRI_ODRV_NODE_M0;
    s_model.odrv_node[1] = CONFIG_KHEPRI_ODRV_NODE_M1;
    xSemaphoreGive(s_model_mux);
    settings_save();
}

static void ui_on_ack(void)
{
    xSemaphoreTake(s_model_mux, portMAX_DELAY);
    uint8_t node = s_model.alarm_node;
    bool was = s_model.alarm_pending;
    kh_model_ack_alarm(&s_model, now_ms());
    xSemaphoreGive(s_model_mux);
    if (was && !s_local.demo) khepri_link_send_cmd(KHN_CMD_ACK_ALARM, node);
}

/* Mise à jour autorisée : moteurs arrêtés (ou régime inconnu) et aucune alerte en cours */
static bool ota_can_update(void)
{
    bool ok = true;
    int64_t t = now_ms();
    xSemaphoreTake(s_model_mux, portMAX_DELAY);
    for (int k = 0; k < 2; k++)
        if (!s_local.demo && kh_fresh(s_model.t_motor[k], t, KH_STALE_MS) && fabsf(s_model.rpm[k]) > 30.0f) ok = false;
    if (s_model.alarm_pending) ok = false;
    xSemaphoreGive(s_model_mux);
    return ok;
}

static void ui_on_wifi_scan(void) { kh_wifi_scan(); }
static void ui_on_wifi_connect(const char *ssid, const char *pass) { kh_wifi_connect(ssid, pass); }
static void ui_on_wifi_forget(void) { kh_wifi_forget(); }
static void ui_on_ota_check(void) { kh_ota_check_now(); }

static void ui_on_brightness(int pct)
{
    set_brightness(pct);
    settings_save();
}

/* ══════════════════════════════════════════════
 *  Radio → modèle
 * ══════════════════════════════════════════════ */
static void on_rx(const khn_hdr_t *h, const khn_frame_t *f, int n, int8_t rssi)
{
    if (s_local.demo) return; /* la simulation locale a la main */
    xSemaphoreTake(s_model_mux, portMAX_DELAY);
    kh_model_apply_packet(&s_model, h, f, n, rssi, now_ms());
    xSemaphoreGive(s_model_mux);
}

/* ══════════════════════════════════════════════
 *  Tâches
 * ══════════════════════════════════════════════ */
/* 10 Hz : simulation éventuelle, puis rafraîchissement de l'écran */
static void ui_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint16_t seq = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
        int64_t t = now_ms();
        kh_model_t snap;
        xSemaphoreTake(s_model_mux, portMAX_DELAY);
        if (s_local.demo) {
            khn_frame_t fr[KHN_MAX_FRAMES];
            int n = kh_demo_frames(t, fr, KHN_MAX_FRAMES);
            khn_hdr_t h = {.version = KHN_VERSION, .type = KHN_T_CAN_BATCH, .seq = seq++, .count = (uint8_t)n,
                           .flags = KHN_HF_DEMO};
            kh_model_apply_packet(&s_model, &h, fr, n, -40, t);
        }
        snap = s_model;
        xSemaphoreGive(s_model_mux);

        /* consigne de régime : vers la simulation, et vers le maître (5 Hz au plus, et à chaque changement) */
        portENTER_CRITICAL(&s_in_lock);
        float thr = s_throttle;
        int64_t thr_ms = s_throttle_ms;
        portEXIT_CRITICAL(&s_in_lock);
        s_local.throttle_pct = thr;
        s_local.throttle_adjust_ms = thr_ms;
        kh_demo_set_throttle(thr);
        static uint32_t sent_pm = UINT32_MAX;
        static int64_t sent_t;
        uint32_t pm = (uint32_t)lroundf(thr * 10.0f);
        if (pm != sent_pm && t - sent_t >= 200 && s_local.bound && !s_local.demo && !kh_wifi_scanning()) {
            if (khepri_link_send_cmd(KHN_CMD_THROTTLE, pm)) sent_pm = pm;
            sent_t = t;
        }

        s_local.bound = khepri_link_bound();
        s_local.binding = khepri_link_binding();
        kh_wifi_get_status(&s_local.wifi);
        kh_ota_get_status(&s_local.ota);
        khepri_link_master_mac(s_local.master_mac);
        /* instantané pour la tâche d'affichage (aucun appel LVGL ici) */
        xSemaphoreTake(s_snap_mux, portMAX_DELAY);
        s_snap_model = snap;
        s_snap_local = s_local;
        s_snap_new = true;
        xSemaphoreGive(s_snap_mux);
    }
}

/* 100 Hz : gestes à l'accéléromètre (poussée → page, roulis → régime) et bouton PWR */
static void input_task(void *arg)
{
    knob_cfg_t kc = {.push_mg = CONFIG_KHEPRI_PUSH_MG,
                     .push_dead_ms = 600,
                     .roll_dead_deg = CONFIG_KHEPRI_ROLL_DEAD_DEG,
                     .roll_full_deg = CONFIG_KHEPRI_ROLL_FULL_DEG,
                     .rate_max_pct_s = CONFIG_KHEPRI_ROLL_RATE_PCT_S,
#ifdef CONFIG_KHEPRI_ROLL_INVERT
                     .invert_roll = true,
#endif
#ifdef CONFIG_KHEPRI_PUSH_INVERT
                     .invert_push = true,
#endif
    };
    knob_init(&s_knob, &kc);
    bool imu = qmi8658_init();
    if (!imu) ESP_LOGW(TAG, "QMI8658 absent : pas de gestes");
    bool btn_prev = false;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
        knob_evt_t e = {0};
        qmi8658_accel_t a;
        if (imu && qmi8658_read_accel(&a)) knob_update(&s_knob, a.ax, a.ay, a.az, 10, &e);
        bool btn = kh_pwr_pressed();
        bool click = btn && !btn_prev;
        btn_prev = btn;
        float d = s_roll_enabled ? e.dthrottle_pct : 0;
        if (e.push || click || d != 0) {
            portENTER_CRITICAL(&s_in_lock);
            s_in_pushes += e.push;
            s_in_clicks += click;
            if (d != 0) {
                float v = s_throttle + d;
                s_throttle = v < 0 ? 0 : v > THROTTLE_MAX_PCT ? THROTTLE_MAX_PCT : v;
                s_throttle_ms = esp_timer_get_time() / 1000;
            }
            portEXIT_CRITICAL(&s_in_lock);
        }
    }
}

/* 1 Hz : appairage, battement de cœur vers le maître (peut bloquer quelques ms) */
static void link_task(void *arg)
{
    int t = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        khepri_link_tick();
        if (++t == 20) kh_ota_mark_valid(); /* 20 s sans plantage : la nouvelle version est gardée */
    }
}

/* Création de tâche vérifiée : un échec (mémoire) ne doit plus passer inaperçu.
 * Les tâches qui n'écrivent jamais en flash peuvent avoir leur pile en PSRAM. */
static void start_task(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio, BaseType_t core,
                       bool psram_stack)
{
    BaseType_t ok;
    if (psram_stack)
        ok = xTaskCreatePinnedToCoreWithCaps(fn, name, stack, NULL, prio, NULL, core, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    else
        ok = xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, NULL, core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "IMPOSSIBLE de créer la tâche %s (%lu o) : mémoire insuffisante", name, (unsigned long)stack);
        abort(); /* redémarrage visible (compteur d'anomalies) plutôt qu'un écran figé */
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
    boot_diag();

    /* ── PARTIE 1 : écran AMOLED CO5300 en QSPI ── */
    ESP_LOGI(TAG, "Bus QSPI de l'écran");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, notify_flush_ready, &disp_drv);
    io_config.pclk_hz = 40 * 1000 * 1000; /* 40 MHz comme le BSP Waveshare */
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

    /* I2C commun + périphériques */
    I2C_master_Init();
    kh_touch_init();
    kh_expander_init();

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
    kh_model_init(&s_model);
    s_model.odrv_node[0] = CONFIG_KHEPRI_ODRV_NODE_M0;
    s_model.odrv_node[1] = CONFIG_KHEPRI_ODRV_NODE_M1;

    const kh_screen_cb_t cb = {.on_bind = ui_on_bind,
                               .on_mark = ui_on_mark,
                               .on_demo = ui_on_demo,
                               .on_ack = ui_on_ack,
                               .on_brightness = ui_on_brightness,
                               .on_wifi_scan = ui_on_wifi_scan,
                               .on_wifi_connect = ui_on_wifi_connect,
                               .on_wifi_forget = ui_on_wifi_forget,
                               .on_ota_check = ui_on_ota_check};
    kh_screen_init(&cb);
    set_brightness(s_local.brightness_pct);
    start_task(lvgl_task, "lvgl", 12 * 1024, 5, 1, false); /* NVS (réglages) : pile interne */

    /* ── PARTIE 3 : radio, entrées, rafraîchissement ── */
    s_local.espnow_channel = CONFIG_KHEPRI_ESPNOW_CHANNEL;
    s_local.thrust_ref_w = CONFIG_KHEPRI_THRUST_REF_W;
    s_local.batt_wh = CONFIG_KHEPRI_BATT_WH;
    s_local.pv_peak_w = CONFIG_KHEPRI_PV_PEAK_W;
    s_local.level_w = CONFIG_KHEPRI_LEVEL_POWER_W;
    khepri_link_init(on_rx);
    /* ── PARTIE 4 : Wi-Fi et mises à jour ── */
    kh_wifi_init();
    kh_ota_init(ota_can_update);
    start_task(ui_task, "ui", 12 * 1024, 4, tskNO_AFFINITY, true);
    start_task(input_task, "input", 8 * 1024, 4, tskNO_AFFINITY, true);
    start_task(link_task, "link", 6 * 1024, 2, tskNO_AFFINITY, false); /* écrit l'otadata : pile interne */

    ESP_LOGI(TAG, "mémoire libre : interne %u o (plus grand bloc %u o), PSRAM %u o",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_LOGI(TAG, "=== KHÉPRI écran pilote prêt (simulation %s) ===", s_local.demo ? "ACTIVE" : "inactive");
}
