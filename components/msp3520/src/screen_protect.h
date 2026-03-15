#pragma once

#include "msp3520_priv.h"

esp_err_t screen_protect_init(msp3520_handle_t h);
void screen_protect_deinit(msp3520_handle_t h);
void screen_protect_set_dim_timeout(msp3520_handle_t h, uint16_t seconds);
void screen_protect_set_off_timeout(msp3520_handle_t h, uint16_t seconds);
void screen_protect_get_status(msp3520_handle_t h, const char **state,
                                uint16_t *dim_s, uint16_t *off_s,
                                uint32_t *idle_ms);
