idf_component_register(
    SRCS "finger-paint.c"
    INCLUDE_DIRS "."
    REQUIRES msp3520 nvs_flash console
)
