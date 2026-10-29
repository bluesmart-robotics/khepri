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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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
#define LVGL_BUF_LINES (LCD_H / 8)
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

static SemaphoreHandle_t s_lvgl_mux;
static SemaphoreHandle_t s_model_mux;
static esp_lcd_panel_io_handle_t s_io;

static kh_model_t s_model;
static kh_local_t s_local;
static knob_t s_knob;
static kh_knob_view_t s_knob_view;

/* ══════════════════════════════════════════════
 *  LVGL (même schéma que la télécommande BSR)
 * ══════════════════════════════════════════════ */
bool kh_lvgl_lock(int timeout_ms)
{
    const TickType_t t = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mux, t) == pdTRUE;
}
void kh_lvgl_unlock(void) { xSemaphoreGiveRecursive(s_lvgl_mux); }

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
    uint32_t delay = LVGL_TASK_MAX_DELAY_MS;
    for (;;) {
        if (kh_lvgl_lock(-1)) {
            delay = lv_timer_handler();
            kh_lvgl_unlock();
        }
        if (delay > LVGL_TASK_MAX_DELAY_MS) delay = LVGL_TASK_MAX_DELAY_MS;
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

        s_local.bound = khepri_link_bound();
        s_local.binding = khepri_link_binding();
        kh_wifi_get_status(&s_local.wifi);
        kh_ota_get_status(&s_local.ota);
        khepri_link_master_mac(s_local.master_mac);
        if (kh_lvgl_lock(50)) {
            kh_screen_update(&snap, &s_local, &s_knob_view, t);
            kh_lvgl_unlock();
        }
    }
}

/* 50 Hz : molette (accéléromètre) et bouton PWR */
static void input_task(void *arg)
{
    knob_cfg_t kc = {.detent_deg = CONFIG_KHEPRI_KNOB_DETENT_DEG,
                     .tilt_on_deg = 15,
                     .tilt_off_deg = 8,
                     .repeat_ms = 450,
#ifdef CONFIG_KHEPRI_KNOB_INVERT
                     .invert = true,
#else
                     .invert = false,
#endif
    };
    knob_init(&s_knob, &kc);
    bool imu = qmi8658_init();
    if (!imu) ESP_LOGW(TAG, "QMI8658 absent : pas de molette");
    bool btn_prev = false;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
        int steps = 0;
        qmi8658_accel_t a;
        if (imu && qmi8658_read_accel(&a)) {
            steps = knob_update(&s_knob, a.ax, a.ay, a.az, 20);
            s_knob_view.valid = knob_valid(&s_knob);
            s_knob_view.tilt_mode = s_knob.tilt_mode;
            s_knob_view.angle_deg = s_knob.angle_deg;
        }
        bool btn = kh_pwr_pressed();
        bool click = btn && !btn_prev;
        btn_prev = btn;
        if ((steps || click) && kh_lvgl_lock(100)) {
            if (steps) kh_screen_knob_step(steps);
            if (click) kh_screen_button();
            kh_lvgl_unlock();
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

    s_lvgl_mux = xSemaphoreCreateRecursiveMutex();
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
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 16 * 1024, NULL, 5, NULL, 1);

    /* ── PARTIE 3 : radio, entrées, rafraîchissement ── */
    s_local.espnow_channel = CONFIG_KHEPRI_ESPNOW_CHANNEL;
    khepri_link_init(on_rx);
    /* ── PARTIE 4 : Wi-Fi et mises à jour ── */
    kh_wifi_init();
    kh_ota_init(ota_can_update);
    xTaskCreate(ui_task, "ui", 8 * 1024, NULL, 4, NULL);
    xTaskCreate(input_task, "input", 4 * 1024, NULL, 4, NULL);
    xTaskCreate(link_task, "link", 4 * 1024, NULL, 2, NULL);

    ESP_LOGI(TAG, "=== KHÉPRI écran pilote prêt (simulation %s) ===", s_local.demo ? "ACTIVE" : "inactive");
}
