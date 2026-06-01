idf_component_register(
    SRCS "display_module.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        driver
        esp_hw_support
        esp_lcd_co5300
        esp_lvgl_port
        lvgl
)
