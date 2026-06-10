/*
 * ESP32-P4C6 Demo Firmware — app_main
 *
 * Initialises every peripheral module then starts the JSON protocol task
 * which listens on TinyUSB CDC and dispatches commands to the modules.
 *
 * Communication channel: USB 2.0 PHY → TinyUSB CDC-ACM (NOT the internal
 * USB Serial/JTAG controller, which may be disabled on some boards via eFuse).
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

#include "protocol.h"
#include "gpio_module.h"
#include "uart_module.h"
#include "i2c_module.h"
#include "emmc_module.h"
#include "wifi_module.h"
#include "display_module.h"
#include "can_module.h"

static const char *TAG = "main";

/* ── Protocol task ─────────────────────────────────────────────────────── */

static char s_line_buf[PROTO_LINE_MAX];
static size_t s_line_pos = 0;

/* Called from the CDC RX callback with freshly received bytes. */
static void process_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            if (s_line_pos > 0) {
                s_line_buf[s_line_pos] = '\0';
                proto_dispatch(s_line_buf, s_line_pos);
                s_line_pos = 0;
            }
        } else {
            if (s_line_pos < PROTO_LINE_MAX - 1) {
                s_line_buf[s_line_pos++] = c;
            }
            /* Silently drop if buffer is full — host sent a malformed line. */
        }
    }
}

/* TinyUSB CDC receive callback (called from USB task context). */
static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)itf;
    (void)event;
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t rx_size = 0;
    if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof(buf), &rx_size) == ESP_OK) {
        process_bytes(buf, rx_size);
    }
}

/* ── Module initialisation ─────────────────────────────────────────────── */

/* Fast modules safe to init before USB comes up. */
static void init_modules_fast(void)
{
    ESP_LOGI(TAG, "Initialising GPIO module...");
    gpio_module_init();

    ESP_LOGI(TAG, "Initialising UART module...");
    uart_module_init();

    ESP_LOGI(TAG, "Initialising CAN module...");
    can_module_init();
}

/* Slow/hardware-dependent modules — called after USB is up so the board
 * is reachable even if one of these hangs or fails. */
static void init_modules_hw(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Initialising I2C module...");
    if (i2c_module_init() != 0) {
        ESP_LOGW(TAG, "I2C module init failed — sensor readings unavailable");
    }

    ESP_LOGI(TAG, "Initialising eMMC module...");
    if (emmc_module_init() != 0) {
        ESP_LOGW(TAG, "eMMC module init failed");
    }

    ESP_LOGI(TAG, "Initialising Wi-Fi module...");
    if (wifi_module_init() != 0) {
        ESP_LOGW(TAG, "Wi-Fi module init failed — C6 not responding on UART link");
    }

    /* Display init is deferred: the CO5300 panel_init busy-waits forever on the
     * MIPI-DSI read FIFO when no panel (or a mis-seated FPC) is present, which
     * pins CPU0 and starves the rest of the system. It is now initialised
     * on-demand by the first display_* command instead (see protocol.c). */
    ESP_LOGI(TAG, "Display module init deferred to first display command");

    proto_send("{\"event\":\"hw_ready\",\"i2c\":1,\"emmc\":1}");
    vTaskDelete(NULL);
}

/* ── USB / TinyUSB setup ───────────────────────────────────────────────── */

static void init_usb(void)
{
    const tinyusb_config_t tusb_cfg = {};
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev               = TINYUSB_USBDEV_0,
        .cdc_port              = TINYUSB_CDC_ACM_0,
        .callback_rx           = cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    ESP_LOGI(TAG, "USB CDC initialised — waiting for host connection");
}

/* ── Entry point ───────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4C6 Demo Firmware v1.0.0 starting...");

    init_modules_fast();
    init_usb();

    /* Brief delay so the host can enumerate the CDC device. */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Banner visible immediately after the host opens the serial port. */
    proto_send("{\"event\":\"ready\",\"firmware\":\"ESP32-P4C6-Demo\","
               "\"version\":\"1.0.0\"}");

    ESP_LOGI(TAG, "Ready. Listening for JSON commands on USB CDC.");

    /* Init slow/hardware modules in a background task so USB stays responsive. */
    xTaskCreate(init_modules_hw, "hw_init", 8192, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
