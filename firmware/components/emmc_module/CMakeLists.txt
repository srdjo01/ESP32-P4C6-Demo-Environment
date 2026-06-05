idf_component_register(
    SRCS "emmc_module.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        driver
        sdmmc
        esp_hw_support
        esp_timer
)
