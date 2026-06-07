idf_component_register(
    SRCS "wifi_module.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        driver
        json
        esp_timer
)
