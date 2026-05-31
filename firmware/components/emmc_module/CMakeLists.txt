idf_component_register(
    SRCS "emmc_module.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        driver
        fatfs
        sdmmc
        esp_ldo_regulator
        esp_timer
)
