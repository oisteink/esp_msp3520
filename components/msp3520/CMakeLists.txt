idf_component_register(
    SRCS "src/msp3520.c"
         "src/ili9488.c"
         "src/xpt2046.c"
         "src/touch_calibration.c"
         "src/console_commands.c"
         "src/backlight.c"
         "src/screen_protect.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_lcd lvgl
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_ledc
                  esp_timer nvs_flash console
)
