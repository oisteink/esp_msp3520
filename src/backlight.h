#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t backlight_init(int gpio, bool active_high);
esp_err_t backlight_set(uint8_t brightness);
esp_err_t backlight_fade(uint8_t brightness, uint32_t fade_ms);
esp_err_t backlight_fade_stop(void);
