idf_component_register(
    SRCS "test_app_main.c"
         "test_screen_protect.c"
         "test_indev_sim.c"
    PRIV_REQUIRES msp3520 unity
                  espressif__esp_lcd_touch esp_timer esp_driver_ledc
                  esp_driver_spi esp_driver_gpio nvs_flash console
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../src"
    WHOLE_ARCHIVE)
