#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t backlight_init(int gpio, bool active_high);
esp_err_t backlight_set(uint8_t brightness);
