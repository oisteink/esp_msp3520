#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 3-point affine calibration: maps raw touch (x,y) to screen (sx,sy)
//   sx = a*x + b*y + c
//   sy = d*x + e*y + f
typedef struct {
    float a, b, c;
    float d, e, f;
    bool valid;
} touch_cal_t;

// Compute affine coefficients from 3 reference point pairs
// raw[3] = raw touch coordinates, screen[3] = expected screen coordinates
// Returns ESP_OK on success, ESP_ERR_INVALID_ARG if points are degenerate
esp_err_t touch_cal_compute(const uint16_t raw_x[3], const uint16_t raw_y[3],
                            const uint16_t scr_x[3], const uint16_t scr_y[3],
                            touch_cal_t *cal);

// Apply calibration transform to raw coordinates
void touch_cal_apply(const touch_cal_t *cal, uint16_t raw_x, uint16_t raw_y,
                     uint16_t *scr_x, uint16_t *scr_y, uint16_t x_max, uint16_t y_max);

// Save calibration to NVS
esp_err_t touch_cal_save(const touch_cal_t *cal);

// Load calibration from NVS (sets cal->valid = false if not found)
esp_err_t touch_cal_load(touch_cal_t *cal);

// Clear calibration from NVS
esp_err_t touch_cal_clear(void);
