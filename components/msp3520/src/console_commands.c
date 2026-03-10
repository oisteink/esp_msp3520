#include "msp3520_priv.h"
#include "xpt2046.h"
#include "backlight.h"

#include "esp_console.h"
#include "esp_log.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "msp3520_cmd";

/* -- Calibration screen -------------------------------------------- */

static msp3520_handle_t cal_handle;
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

    /* Use stored raw ADC values from process_coordinates_cb, not the
       mapped screen coordinates that LVGL sees */
    cal_raw_x[cal_point_idx] = cal_handle->last_raw_x;
    cal_raw_y[cal_point_idx] = cal_handle->last_raw_y;

    ESP_LOGI(TAG, "Cal point %d: raw=(%u, %u) screen=(%u, %u)",
             cal_point_idx, cal_handle->last_raw_x, cal_handle->last_raw_y,
             cal_screen_x[cal_point_idx], cal_screen_y[cal_point_idx]);

    cal_point_idx++;
    cal_wait_release = true;

    if (cal_point_idx >= 3) {
        cal_active = false;

        touch_cal_t cal;
        esp_err_t err = touch_cal_compute(cal_raw_x, cal_raw_y,
                                          cal_screen_x, cal_screen_y, &cal);
        if (err == ESP_OK) {
            cal_handle->cal = cal;
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

esp_err_t msp3520_start_calibration(msp3520_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    cal_handle = handle;

    msp3520_lvgl_lock(handle, 0);

    handle->cal.valid = false;

    main_screen = lv_screen_active();

    if (!cal_screen) {
        cal_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(cal_screen, lv_color_white(), 0);
        lv_obj_set_size(cal_screen, MSP3520_H_RES, MSP3520_V_RES);

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

    msp3520_lvgl_unlock(handle);
    return ESP_OK;
}

/* -- Command handlers ---------------------------------------------- */

static int cmd_touch(void *ctx, int argc, char **argv)
{
    msp3520_handle_t h = (msp3520_handle_t)ctx;

    if (argc == 1) {
        printf("z_threshold=%u\n", esp_lcd_touch_xpt2046_get_z_threshold());
        if (h->cal.valid) {
            printf("Calibration: active\n");
            printf("  a=%.6f b=%.6f c=%.1f\n", h->cal.a, h->cal.b, h->cal.c);
            printf("  d=%.6f e=%.6f f=%.1f\n", h->cal.d, h->cal.e, h->cal.f);
        } else {
            printf("Calibration: none\n");
        }
        bool swap, mx, my;
        esp_lcd_touch_get_swap_xy(h->touch, &swap);
        esp_lcd_touch_get_mirror_x(h->touch, &mx);
        esp_lcd_touch_get_mirror_y(h->touch, &my);
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

    if (strcmp(sub, "rate") == 0) {
        if (argc != 3) {
            printf("Usage: touch rate <ms>\n");
            return 1;
        }
        uint32_t ms = (uint32_t)atoi(argv[2]);
        if (ms < 1 || ms > 100) {
            printf("Range: 1-100 ms\n");
            return 1;
        }
        msp3520_lvgl_lock(h, 0);
        lv_timer_t *tmr = lv_indev_get_read_timer(h->indev);
        if (tmr) {
            lv_timer_set_period(tmr, ms);
            printf("Touch read period set to %"PRIu32"ms\n", ms);
        } else {
            printf("Error: no indev timer\n");
        }
        msp3520_lvgl_unlock(h);
        return 0;
    }

    if (strcmp(sub, "cal") == 0) {
        if (argc == 2) {
            if (h->cal.valid) {
                printf("a=%.6f b=%.6f c=%.1f\n", h->cal.a, h->cal.b, h->cal.c);
                printf("d=%.6f e=%.6f f=%.1f\n", h->cal.d, h->cal.e, h->cal.f);
            } else {
                printf("No calibration data\n");
            }
            return 0;
        }
        const char *action = argv[2];
        if (strcmp(action, "start") == 0) {
            printf("Starting calibration...\n");
            msp3520_start_calibration(h);
        } else if (strcmp(action, "show") == 0) {
            if (h->cal.valid) {
                printf("a=%.6f b=%.6f c=%.1f\n", h->cal.a, h->cal.b, h->cal.c);
                printf("d=%.6f e=%.6f f=%.1f\n", h->cal.d, h->cal.e, h->cal.f);
            } else {
                printf("No calibration data\n");
            }
        } else if (strcmp(action, "clear") == 0) {
            msp3520_clear_calibration(h);
            printf("Calibration cleared\n");
        } else {
            printf("Usage: touch cal [start|show|clear]\n");
            return 1;
        }
        return 0;
    }

    if (argc == 3) {
        bool val = atoi(argv[2]) != 0;
        if      (strcmp(sub, "swap_xy") == 0)  esp_lcd_touch_set_swap_xy(h->touch, val);
        else if (strcmp(sub, "mirror_x") == 0) esp_lcd_touch_set_mirror_x(h->touch, val);
        else if (strcmp(sub, "mirror_y") == 0) esp_lcd_touch_set_mirror_y(h->touch, val);
        else {
            printf("Unknown: %s\n", sub);
            return 1;
        }
        printf("Set %s=%d\n", sub, val);
        return 0;
    }

    printf("Usage: touch [z <val>|rate <ms>|cal [start|show|clear]|swap_xy|mirror_x|mirror_y <0|1>]\n");
    return 1;
}

static int cmd_display(void *ctx, int argc, char **argv)
{
    msp3520_handle_t h = (msp3520_handle_t)ctx;

    if (argc == 1) {
        printf("Display: %dx%d\n", MSP3520_H_RES, MSP3520_V_RES);
        printf("Use: display backlight <0-100>\n");
        printf("     display rotation [swap_xy|mirror_x|mirror_y] [0|1]\n");
#if LV_USE_PERF_MONITOR || LV_USE_MEM_MONITOR
        printf("     display perf <on|off>\n");
#endif
        return 0;
    }

    const char *sub = argv[1];

#if LV_USE_PERF_MONITOR || LV_USE_MEM_MONITOR
    if (strcmp(sub, "perf") == 0) {
        if (argc != 3) {
            printf("Usage: display perf <on|off>\n");
            return 1;
        }
        msp3520_lvgl_lock(h, 0);
        if (strcmp(argv[2], "on") == 0) {
#if LV_USE_PERF_MONITOR
            lv_sysmon_show_performance(h->display);
#endif
#if LV_USE_MEM_MONITOR
            lv_sysmon_show_memory(h->display);
#endif
            printf("Perf monitor: on\n");
        } else {
#if LV_USE_PERF_MONITOR
            lv_sysmon_hide_performance(h->display);
#endif
#if LV_USE_MEM_MONITOR
            lv_sysmon_hide_memory(h->display);
#endif
            printf("Perf monitor: off\n");
        }
        msp3520_lvgl_unlock(h);
        return 0;
    }
#endif

    if (strcmp(sub, "backlight") == 0) {
        if (argc != 3) {
            printf("Usage: display backlight <0-100>\n");
            return 1;
        }
        uint8_t val = (uint8_t)atoi(argv[2]);
        msp3520_set_backlight(h, val);
        printf("Backlight set to %u%%\n", val);
        return 0;
    }

    if (strcmp(sub, "rotation") == 0) {
        if (argc < 4) {
            printf("Usage: display rotation [swap_xy|mirror_x|mirror_y] [0|1]\n");
            return 1;
        }
        bool val = atoi(argv[3]) != 0;
        if (strcmp(argv[2], "swap_xy") == 0) {
            esp_lcd_panel_swap_xy(h->panel, val);
        } else if (strcmp(argv[2], "mirror_x") == 0 || strcmp(argv[2], "mirror_y") == 0) {
            /* Mirror requires both values; for simplicity just set both via the API */
            esp_lcd_panel_mirror(h->panel, val, val);
        } else {
            printf("Unknown flag: %s\n", argv[2]);
            return 1;
        }
        printf("Set %s=%d\n", argv[2], val);
        return 0;
    }

    printf("Unknown subcommand: %s\n", sub);
    return 1;
}

esp_err_t msp3520_register_console_commands(msp3520_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "touch",
        .help = "Touch config, calibration, and flags",
        .hint = "[z <val>|rate <ms>|cal [start|show|clear]|swap_xy|mirror_x|mirror_y <0|1>]",
        .func_w_context = cmd_touch,
        .context = handle,
    });

    esp_console_cmd_register(&(esp_console_cmd_t){
        .command = "display",
        .help = "Display control (backlight, rotation, perf)",
        .hint = "[backlight <0-100>|rotation <flag> <0|1>|perf <on|off>]",
        .func_w_context = cmd_display,
        .context = handle,
    });

    ESP_LOGI(TAG, "console commands registered");
    return ESP_OK;
}
