#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "touch_calibration.h"

static const char *TAG = "console";

/* -- Calibration screen -------------------------------------------- */

static app_context_t *cal_app_ctx;
static _lock_t *cal_lvgl_lock;
static uint16_t cal_h_res;
static uint16_t cal_v_res;

static const uint16_t cal_screen_x[3] = { 40, 280, 160 };
static const uint16_t cal_screen_y[3] = { 40, 40, 440 };

static lv_obj_t *cal_label = NULL;
static lv_obj_t *cal_crosshair = NULL;
static uint8_t cal_point_idx = 0;
static uint16_t cal_raw_x[3];
static uint16_t cal_raw_y[3];
static bool cal_active = false;
static bool cal_wait_release = false;
static lv_obj_t *cal_screen = NULL;
static lv_obj_t *main_screen = NULL;

static void draw_crosshair(lv_obj_t *parent, uint16_t x, uint16_t y)
{
    if (cal_crosshair) {
        lv_obj_del(cal_crosshair);
    }
    cal_crosshair = lv_label_create(parent);
    lv_label_set_text(cal_crosshair, "+");
    lv_obj_set_style_text_font(cal_crosshair, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(cal_crosshair, lv_color_make(0xFF, 0x00, 0x00), 0);
    lv_obj_set_pos(cal_crosshair, x - 8, y - 14);
}

static void cal_return_timer_cb(lv_timer_t *timer)
{
    if (main_screen) {
        lv_screen_load(main_screen);
    }
    lv_timer_del(timer);
}

static void cal_release_cb(lv_event_t *e)
{
    cal_wait_release = false;
}

static void cal_touch_cb(lv_event_t *e)
{
    if (!cal_active || cal_wait_release) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    cal_raw_x[cal_point_idx] = (uint16_t)p.x;
    cal_raw_y[cal_point_idx] = (uint16_t)p.y;

    ESP_LOGI(TAG, "Cal point %d: raw=(%u, %u) screen=(%u, %u)",
             cal_point_idx, (uint16_t)p.x, (uint16_t)p.y,
             cal_screen_x[cal_point_idx], cal_screen_y[cal_point_idx]);

    cal_point_idx++;
    cal_wait_release = true;

    if (cal_point_idx >= 3) {
        cal_active = false;

        touch_cal_t cal;
        esp_err_t err = touch_cal_compute(cal_raw_x, cal_raw_y,
                                          cal_screen_x, cal_screen_y, &cal);
        if (err == ESP_OK) {
            cal_app_ctx->cal = cal;
            touch_cal_save(&cal);
            lv_label_set_text(cal_label, "Calibration OK!\nSaved to NVS.");
        } else {
            lv_label_set_text(cal_label, "Calibration FAILED.\nTry again.");
        }

        if (cal_crosshair) {
            lv_obj_del(cal_crosshair);
            cal_crosshair = NULL;
        }

        lv_timer_create(cal_return_timer_cb, 2000, NULL);
    } else {
        lv_obj_t *scr = lv_obj_get_parent(cal_label);
        draw_crosshair(scr, cal_screen_x[cal_point_idx], cal_screen_y[cal_point_idx]);
        lv_label_set_text_fmt(cal_label, "Tap crosshair %d/3", cal_point_idx + 1);
    }
}

void console_set_cal_context(app_context_t *ctx, _lock_t *lvgl_lock,
                             uint16_t h_res, uint16_t v_res)
{
    cal_app_ctx = ctx;
    cal_lvgl_lock = lvgl_lock;
    cal_h_res = h_res;
    cal_v_res = v_res;
}

void console_start_calibration(app_context_t *ctx, _lock_t *lvgl_lock,
                               uint16_t h_res, uint16_t v_res)
{
    cal_app_ctx = ctx;
    cal_lvgl_lock = lvgl_lock;
    cal_h_res = h_res;
    cal_v_res = v_res;

    _lock_acquire(lvgl_lock);

    ctx->cal.valid = false;

    main_screen = lv_screen_active();

    if (!cal_screen) {
        cal_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(cal_screen, lv_color_white(), 0);
        lv_obj_set_size(cal_screen, h_res, v_res);

        cal_label = lv_label_create(cal_screen);
        lv_obj_set_style_text_font(cal_label, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(cal_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(cal_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(cal_label, LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_event_cb(cal_screen, cal_touch_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(cal_screen, cal_release_cb, LV_EVENT_RELEASED, NULL);
    }

    cal_point_idx = 0;
    cal_active = true;
    cal_wait_release = false;
    lv_label_set_text(cal_label, "Tap crosshair 1/3");

    draw_crosshair(cal_screen, cal_screen_x[0], cal_screen_y[0]);

    lv_screen_load(cal_screen);

    _lock_release(lvgl_lock);
}

/* -- Command handlers ---------------------------------------------- */

static int cmd_log_level(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: log_level <tag> <none|error|warn|info|debug|verbose>\n");
        return 1;
    }
    const char *tag = argv[1];
    const char *lvl = argv[2];
    esp_log_level_t level;
    if      (strcmp(lvl, "none") == 0)    level = ESP_LOG_NONE;
    else if (strcmp(lvl, "error") == 0)   level = ESP_LOG_ERROR;
    else if (strcmp(lvl, "warn") == 0)    level = ESP_LOG_WARN;
    else if (strcmp(lvl, "info") == 0)    level = ESP_LOG_INFO;
    else if (strcmp(lvl, "debug") == 0)   level = ESP_LOG_DEBUG;
    else if (strcmp(lvl, "verbose") == 0) level = ESP_LOG_VERBOSE;
    else {
        printf("Unknown level: %s\n", lvl);
        return 1;
    }
    esp_log_level_set(tag, level);
    printf("Set %s to %s\n", tag, lvl);
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    int64_t uptime_us = esp_timer_get_time();
    printf("Chip:      ESP32-S3 rev %d.%d, %d core(s)\n",
           chip.revision / 100, chip.revision % 100, chip.cores);
    printf("Heap free: %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("Heap min:  %lu bytes\n", (unsigned long)esp_get_minimum_free_heap_size());
    printf("Uptime:    %lld s\n", uptime_us / 1000000);
    return 0;
}

static int cmd_touch(void *ctx, int argc, char **argv)
{
    app_context_t *app = (app_context_t *)ctx;

    if (argc == 1) {
        printf("z_threshold=%u\n", esp_lcd_touch_xpt2046_get_z_threshold());
        if (app->cal.valid) {
            printf("Calibration: active\n");
            printf("  a=%.6f b=%.6f c=%.1f\n", app->cal.a, app->cal.b, app->cal.c);
            printf("  d=%.6f e=%.6f f=%.1f\n", app->cal.d, app->cal.e, app->cal.f);
        } else {
            printf("Calibration: none\n");
        }
        bool swap, mx, my;
        esp_lcd_touch_get_swap_xy(app->touch, &swap);
        esp_lcd_touch_get_mirror_x(app->touch, &mx);
        esp_lcd_touch_get_mirror_y(app->touch, &my);
        printf("Flags: swap_xy=%d mirror_x=%d mirror_y=%d\n", swap, mx, my);
        return 0;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "z") == 0) {
        if (argc != 3) {
            printf("Usage: touch z <value>\n");
            return 1;
        }
        uint16_t val = (uint16_t)atoi(argv[2]);
        esp_lcd_touch_xpt2046_set_z_threshold(val);
        touch_z_threshold_save(val);
        printf("Set z_threshold=%u (saved)\n", val);
        return 0;
    }

    if (strcmp(sub, "cal") == 0) {
        if (argc == 2) {
            if (app->cal.valid) {
                printf("a=%.6f b=%.6f c=%.1f\n", app->cal.a, app->cal.b, app->cal.c);
                printf("d=%.6f e=%.6f f=%.1f\n", app->cal.d, app->cal.e, app->cal.f);
            } else {
                printf("No calibration data\n");
            }
            return 0;
        }
        const char *action = argv[2];
        if (strcmp(action, "start") == 0) {
            printf("Starting calibration...\n");
            console_start_calibration(app, cal_lvgl_lock, cal_h_res, cal_v_res);
        } else if (strcmp(action, "show") == 0) {
            if (app->cal.valid) {
                printf("a=%.6f b=%.6f c=%.1f\n", app->cal.a, app->cal.b, app->cal.c);
                printf("d=%.6f e=%.6f f=%.1f\n", app->cal.d, app->cal.e, app->cal.f);
            } else {
                printf("No calibration data\n");
            }
        } else if (strcmp(action, "clear") == 0) {
            app->cal.valid = false;
            touch_cal_clear();
            printf("Calibration cleared\n");
        } else {
            printf("Usage: touch cal [start|show|clear]\n");
            return 1;
        }
        return 0;
    }

    if (argc == 3) {
        bool val = atoi(argv[2]) != 0;
        if      (strcmp(sub, "swap_xy") == 0)  esp_lcd_touch_set_swap_xy(app->touch, val);
        else if (strcmp(sub, "mirror_x") == 0) esp_lcd_touch_set_mirror_x(app->touch, val);
        else if (strcmp(sub, "mirror_y") == 0) esp_lcd_touch_set_mirror_y(app->touch, val);
        else {
            printf("Unknown: %s\n", sub);
            return 1;
        }
        printf("Set %s=%d\n", sub, val);
        return 0;
    }

    printf("Usage: touch [z <val>|cal [start|show|clear]|swap_xy|mirror_x|mirror_y <0|1>]\n");
    return 1;
}

static int cmd_rotation(void *ctx, int argc, char **argv)
{
    app_context_t *app = (app_context_t *)ctx;
    if (argc == 1) {
        printf("swap_xy=%d  mirror_x=%d  mirror_y=%d\n",
               app->swap_xy, app->mirror_x, app->mirror_y);
        return 0;
    }
    if (argc != 3) {
        printf("Usage: rotation [swap_xy|mirror_x|mirror_y] [0|1]\n");
        return 1;
    }
    bool val = atoi(argv[2]) != 0;
    if (strcmp(argv[1], "swap_xy") == 0) {
        app->swap_xy = val;
        esp_lcd_panel_swap_xy(app->panel, val);
    } else if (strcmp(argv[1], "mirror_x") == 0) {
        app->mirror_x = val;
        esp_lcd_panel_mirror(app->panel, app->mirror_x, app->mirror_y);
    } else if (strcmp(argv[1], "mirror_y") == 0) {
        app->mirror_y = val;
        esp_lcd_panel_mirror(app->panel, app->mirror_x, app->mirror_y);
    } else {
        printf("Unknown flag: %s\n", argv[1]);
        return 1;
    }
    printf("Set %s=%d\n", argv[1], val);
    return 0;
}

static int cmd_debug(int argc, char **argv)
{
    static bool debug_on = false;
    debug_on = !debug_on;
    esp_log_level_t lvl = debug_on ? ESP_LOG_DEBUG : ESP_LOG_INFO;
    esp_log_level_set("app", lvl);
    esp_log_level_set("console", lvl);
    esp_log_level_set("ili9488", lvl);
    esp_log_level_set("xpt2046", lvl);
    printf("Debug %s\n", debug_on ? "ON" : "OFF");
    return 0;
}

/* -- Init ---------------------------------------------------------- */

void console_init(app_context_t *ctx)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "tft> ";
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    esp_console_register_help_command();

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "log_level", .help = "Set log level: log_level <tag> <level>",
        .hint = "<tag> <none|error|warn|info|debug|verbose>", .func = cmd_log_level });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "info", .help = "Show system info", .func = cmd_info });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "touch", .help = "Touch config, calibration, and flags",
        .hint = "[z <val>|cal [start|show|clear]|swap_xy|mirror_x|mirror_y <0|1>]",
        .func_w_context = cmd_touch, .context = ctx });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "rotation", .help = "Get/set display rotation flags",
        .hint = "[swap_xy|mirror_x|mirror_y] [0|1]",
        .func_w_context = cmd_rotation, .context = ctx });
    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "debug", .help = "Toggle debug logging", .func = cmd_debug });

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
