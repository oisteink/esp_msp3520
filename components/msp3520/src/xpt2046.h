#pragma once

#include "esp_lcd_touch.h"
#include "esp_lcd_panel_io.h"

#define XPT2046_SPI_CLOCK_HZ (1 * 1000 * 1000)

#define XPT2046_SPI_IO_CONFIG(touch_cs)             \
    {                                               \
        .cs_gpio_num = (gpio_num_t)touch_cs,        \
        .dc_gpio_num = GPIO_NUM_NC,                 \
        .spi_mode = 0,                              \
        .pclk_hz = XPT2046_SPI_CLOCK_HZ,            \
        .trans_queue_depth = 3,                     \
        .on_color_trans_done = NULL,                \
        .user_ctx = NULL,                           \
        .lcd_cmd_bits = 8,                          \
        .lcd_param_bits = 8,                        \
        .flags =                                    \
        {                                           \
            .dc_high_on_cmd = 0,                    \
            .dc_low_on_data = 0,                    \
            .dc_low_on_param = 0,                   \
            .octal_mode = 0,                        \
            .quad_mode = 0,                         \
            .sio_mode = 0,                          \
            .lsb_first = 0,                         \
            .cs_high_active = 0                     \
        }                                           \
    }

esp_err_t esp_lcd_touch_new_spi_xpt2046(const esp_lcd_panel_io_handle_t io,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *out_touch);

void esp_lcd_touch_xpt2046_set_z_threshold(uint16_t threshold);
uint16_t esp_lcd_touch_xpt2046_get_z_threshold(void);
void esp_lcd_touch_xpt2046_notify_touch(void);
