#pragma once

#include "msp3520.h"
#include "touch_calibration.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct msp3520_t {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_touch_handle_t touch;
    esp_lcd_panel_io_handle_t touch_io;
    lv_display_t *display;
    lv_indev_t *indev;
    touch_cal_t cal;
    msp3520_config_t config;
    bool display_bus_initialized;
    bool touch_bus_initialized;
    uint16_t last_raw_x;
    uint16_t last_raw_y;
    SemaphoreHandle_t lvgl_mutex;
    esp_timer_handle_t lvgl_tick_timer;
    TaskHandle_t lvgl_task_handle;
};
