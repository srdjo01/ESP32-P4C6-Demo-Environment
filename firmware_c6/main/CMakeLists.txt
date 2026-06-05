idf_component_register(
    SRCS
        "c6_main.c"
        "ble_scan.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        driver
        json
        bt
        esp_timer
)
