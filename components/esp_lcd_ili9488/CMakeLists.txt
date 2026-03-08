idf_component_register(
    SRCS "esp_lcd_ili9488.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_lcd"
    PRIV_REQUIRES "esp_driver_gpio"
)
