#pragma once

#include "msp3520_priv.h"

esp_err_t screen_protect_init(msp3520_handle_t h);
void screen_protect_deinit(msp3520_handle_t h);
void screen_protect_set_dim_timeout(msp3520_handle_t h, uint8_t minutes);
void screen_protect_set_off_timeout(msp3520_handle_t h, uint8_t minutes);
void screen_protect_get_status(msp3520_handle_t h, const char **state,
                                uint8_t *dim_min, uint8_t *off_min,
                                uint32_t *idle_ms);
