idf_component_register(
    SRCS "paint.c"
    INCLUDE_DIRS "."
    REQUIRES esp_msp3520 nvs_flash console
)
