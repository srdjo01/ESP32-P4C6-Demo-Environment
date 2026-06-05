idf_component_register(
    SRCS "wifi_module.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_wifi
        esp_wifi_remote
        esp_netif
        esp_event
        lwip
)
