#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "touch_calibration.h"

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    touch_cal_t cal;
} app_context_t;

// Register all console commands and start the REPL
void console_init(app_context_t *ctx);

// Store calibration context (call before console_init)
void console_set_cal_context(app_context_t *ctx, _lock_t *lvgl_lock,
                             uint16_t h_res, uint16_t v_res);

// Start the 3-point calibration screen (called from touch command)
void console_start_calibration(app_context_t *ctx, _lock_t *lvgl_lock,
                               uint16_t h_res, uint16_t v_res);
