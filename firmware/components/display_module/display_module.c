#include "display_module.h"

#include <string.h>
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_co5300.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "driver/i2c_master.h"

static const char *TAG = "display_module";

/* ── Hardware constants ──────────────────────────────────────────────────── */

/* LDO channel 3 at 2500 mV powers the display (from test documentation). */
#define DISP_LDO_CHANNEL   3
#define DISP_LDO_MV        2500

/* CO5300 physical resolution. */
#define DISP_WIDTH   466
#define DISP_HEIGHT  466

/* Panel reset GPIO — confirmed from co5300_test reference (PIN_RST=13).
 * Without a hardware reset pulse the CO5300 does not initialise reliably. */
#define DISP_RST_GPIO  13

/* MIPI DSI configuration — 1 data lane @ 480 Mbps, matching the proven
 * co5300_test reference (CO5300_PANEL_BUS_DSI_1CH_CONFIG). */
#define MIPI_DSI_LANE_NUM         1
#define MIPI_DSI_LANE_BITRATE_MBS 480

/* Touch controller CST820B (on a separate I2C bus from the sensors). */
#define TOUCH_I2C_PORT   I2C_NUM_1
#define TOUCH_SDA_GPIO   23
#define TOUCH_SCL_GPIO   22
#define TOUCH_I2C_ADDR   0x15

/* ── Module state ────────────────────────────────────────────────────────── */

static esp_ldo_channel_handle_t  s_ldo        = NULL;
static esp_lcd_panel_handle_t    s_panel      = NULL;
static lv_disp_t                *s_disp       = NULL;
static lv_obj_t                 *s_scr_pattern = NULL;
static lv_obj_t                 *s_scr_text    = NULL;
static lv_obj_t                 *s_label_text  = NULL;
static bool                      s_init        = false;

/* ── Test-pattern screen ─────────────────────────────────────────────────── */

/* Eight classic colour bars in RGB565. */
static const lv_color_t BAR_COLORS[] = {
    LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),  /* White   */
    LV_COLOR_MAKE(0xFF, 0xFF, 0x00),  /* Yellow  */
    LV_COLOR_MAKE(0x00, 0xFF, 0xFF),  /* Cyan    */
    LV_COLOR_MAKE(0x00, 0xFF, 0x00),  /* Green   */
    LV_COLOR_MAKE(0xFF, 0x00, 0xFF),  /* Magenta */
    LV_COLOR_MAKE(0xFF, 0x00, 0x00),  /* Red     */
    LV_COLOR_MAKE(0x00, 0x00, 0xFF),  /* Blue    */
    LV_COLOR_MAKE(0x00, 0x00, 0x00),  /* Black   */
};
#define BAR_COUNT (sizeof(BAR_COLORS) / sizeof(BAR_COLORS[0]))

static void create_pattern_screen(void)
{
    s_scr_pattern = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_pattern, lv_color_black(), LV_PART_MAIN);

    lv_coord_t bar_h = DISP_HEIGHT / BAR_COUNT;
    for (int i = 0; i < (int)BAR_COUNT; i++) {
        lv_obj_t *bar = lv_obj_create(s_scr_pattern);
        lv_obj_set_size(bar, DISP_WIDTH, bar_h);
        lv_obj_set_pos(bar, 0, i * bar_h);
        lv_obj_set_style_bg_color(bar, BAR_COLORS[i], LV_PART_MAIN);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    }
}

static void create_text_screen(void)
{
    s_scr_text = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_text, lv_color_black(), LV_PART_MAIN);

    s_label_text = lv_label_create(s_scr_text);
    lv_label_set_long_mode(s_label_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_label_text, DISP_WIDTH - 20);
    lv_obj_set_style_text_color(s_label_text, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_label_text, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_label_text, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_label_text, "");
}

/* ── Panel presence probe ──────────────────────────────────────────────────
 *
 * The CO5300's panel_init busy-waits on the MIPI-DSI read FIFO; with no panel
 * (or a mis-seated FPC) that wait never returns, which pins the CPU until the
 * watchdog reboots the board (and drops the USB CDC link — the host GUI then
 * sees the board vanish, i.e. "crash"). There is no read-back on the DSI panel
 * itself, so we detect the display board via its CST820B touch controller,
 * which sits on the same FPC/adapter (I2C_NUM_1, addr 0x15). If the touch
 * controller does not ACK, no display is attached: bail out cleanly instead of
 * entering the hanging panel_init. */
static bool display_panel_present(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = TOUCH_I2C_PORT,
        .sda_io_num                   = TOUCH_SDA_GPIO,
        .scl_io_num                   = TOUCH_SCL_GPIO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "touch-bus probe setup failed — assuming no display");
        return false;
    }
    esp_err_t probe = i2c_master_probe(bus, TOUCH_I2C_ADDR, 100);
    i2c_del_master_bus(bus);
    return probe == ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int display_module_init(void)
{
    esp_err_t ret;

    if (s_init) return 0;   /* idempotent — safe to call from on-demand handlers */

    /* Cheap, non-blocking presence check before the hang-prone panel init. */
    if (!display_panel_present()) {
        ESP_LOGW(TAG, "No display detected (CST820B touch did not ACK) — "
                      "skipping panel init, board stays responsive");
        return -1;
    }

    /* Enable LDO channel 3 for display power. */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = DISP_LDO_CHANNEL,
        .voltage_mv = DISP_LDO_MV,
    };
    ret = esp_ldo_acquire_channel(&ldo_cfg, &s_ldo);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable LDO ch%d: %s",
                 DISP_LDO_CHANNEL, esp_err_to_name(ret));
        return -1;
    }
    ESP_LOGI(TAG, "LDO ch%d enabled at %d mV", DISP_LDO_CHANNEL, DISP_LDO_MV);

    /* ── MIPI DSI bus ── */
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
        .bus_id           = 0,
        .num_data_lanes   = MIPI_DSI_LANE_NUM,
        .phy_clk_src      = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = MIPI_DSI_LANE_BITRATE_MBS,
    };
    ret = esp_lcd_new_dsi_bus(&dsi_bus_cfg, &dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DSI bus init failed: %s", esp_err_to_name(ret));
        esp_ldo_release_channel(s_ldo);
        return -1;
    }

    /* ── CO5300 panel IO (DBI over DSI) ── */
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel IO init failed: %s", esp_err_to_name(ret));
        esp_ldo_release_channel(s_ldo);
        return -1;
    }

    /* ── CO5300 panel ── */
    esp_lcd_dpi_panel_config_t dpi_cfg = CO5300_466_466_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    co5300_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus    = dsi_bus,
            .dpi_config = &dpi_cfg,
        },
        .flags.use_mipi_interface = 1,
    };
    esp_lcd_panel_dev_config_t panel_dev_cfg = {
        .bits_per_pixel = 16,
        .reset_gpio_num = DISP_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .vendor_config  = &vendor_cfg,
    };
    ret = esp_lcd_new_panel_co5300(io, &panel_dev_cfg, &s_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CO5300 panel init failed: %s", esp_err_to_name(ret));
        esp_ldo_release_channel(s_ldo);
        return -1;
    }

    /* Non-fatal: a failure here must NOT abort the board (would cause a reboot
     * loop and kill the USB CDC link). Log and bail out cleanly instead. */
    ret = esp_lcd_panel_reset(s_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "panel_reset failed: %s", esp_err_to_name(ret));
        return -1;
    }
    ret = esp_lcd_panel_init(s_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "panel_init failed: %s", esp_err_to_name(ret));
        return -1;
    }
    ESP_LOGI(TAG, "CO5300 panel reset + init OK");
    /* Note: CO5300 over MIPI DPI turns on during init — it does NOT support
     * esp_lcd_panel_disp_on_off (the reference co5300_test omits it too). */

    /* ── LVGL port ── */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(ret));
        return -1;
    }
    ESP_LOGI(TAG, "LVGL port init OK, adding display...");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = io,
        .panel_handle  = s_panel,
        .buffer_size   = DISP_WIDTH * DISP_HEIGHT,  /* full-screen buffer in PSRAM */
        .double_buffer = false,
        .hres          = DISP_WIDTH,
        .vres          = DISP_HEIGHT,
        .monochrome    = false,
        .rotation      = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags         = { .buff_dma = false, .buff_spiram = true },
    };
    /* CO5300 is a MIPI-DSI (DPI) panel — it must use lvgl_port_add_disp_dsi,
     * which registers the DPI flush callback. The generic lvgl_port_add_disp
     * tries to flush via panel_io (a NULL op on DPI panels) and crashes. */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = { .avoid_tearing = false },
    };
    s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_dsi failed");
        return -1;
    }
    ESP_LOGI(TAG, "LVGL DSI display added");

    /* Build both screens up-front so switching is instant. */
    lvgl_port_lock(0);
    create_pattern_screen();
    create_text_screen();
    lvgl_port_unlock();

    s_init = true;
    ESP_LOGI(TAG, "Display module ready (%dx%d RGB565, CO5300 + LVGL)",
             DISP_WIDTH, DISP_HEIGHT);
    return 0;
}

int display_module_show_pattern(void)
{
    if (!s_init) return -1;
    lvgl_port_lock(0);
    lv_disp_load_scr(s_scr_pattern);
    lvgl_port_unlock();
    return 0;
}

int display_module_show_text(const char *text)
{
    if (!s_init) return -1;
    lvgl_port_lock(0);
    lv_label_set_text(s_label_text, text);
    lv_disp_load_scr(s_scr_text);
    lvgl_port_unlock();
    return 0;
}

void display_module_clear(void)
{
    if (!s_init) return;
    display_module_show_text("");
}
