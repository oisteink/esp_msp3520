idf_component_register(SRCS "ili9488-test.c" "touch_calibration.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp_lcd_ili9488" "esp_driver_spi" "lvgl" "esp_timer"
                             "xpt2046" "espressif__esp_lcd_touch"
                             "console" "nvs_flash")
