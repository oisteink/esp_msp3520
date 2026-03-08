#pragma once

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create LCD panel for ILI9488 (SPI interface)
 *
 * @param[in] io LCD panel IO handle (from esp_lcd_new_panel_io_spi)
 * @param[in] panel_dev_config General panel device configuration
 *            - bits_per_pixel: must be 18 or 24 (both use RGB666, 3 bytes/pixel)
 *            - rgb_ele_order: LCD_RGB_ELEMENT_ORDER_RGB or _BGR
 *            - reset_gpio_num: GPIO for hardware reset, or -1 for software reset
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *      - ESP_ERR_INVALID_ARG   if parameter is invalid
 *      - ESP_ERR_NO_MEM        if out of memory
 *      - ESP_OK                on success
 */
esp_err_t esp_lcd_new_panel_ili9488(const esp_lcd_panel_io_handle_t io,
                                     const esp_lcd_panel_dev_config_t *panel_dev_config,
                                     esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
