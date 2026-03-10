#pragma once

#include "msp3520.h"
#include "touch_calibration.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

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
};
