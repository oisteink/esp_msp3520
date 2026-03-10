idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES msp3520 nvs_flash console
)
