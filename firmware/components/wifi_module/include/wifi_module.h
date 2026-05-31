#pragma once

#include <stdint.h>

/*
 * wifi_module — Wi-Fi via ESP32-C6 co-processor (ESP-Hosted over SDIO)
 *
 * The ESP32-P4 has no native radio. Wi-Fi is provided by the companion
 * ESP32-C6 connected over an SDIO bus.  The ESP-IDF `esp_hosted` component
 * makes this transparent: after init, the standard `esp_wifi` API works as
 * if the radio were local.
 *
 * Setup requirement
 * ─────────────────
 *  1. Flash the ESP-Hosted "slave" firmware to the ESP32-C6.
 *  2. Enable CONFIG_ESP_HOSTED_ENABLED=y in sdkconfig and configure the SDIO
 *     GPIO map to match the board schematic.
 *  3. Call wifi_module_init() — it will return -1 until the above is done.
 *
 * If ESP-Hosted is not configured the module degrades gracefully: every
 * function returns -1 and logs a warning.
 */

/* One scan result entry. */
typedef struct {
    char ssid[33];       /* null-terminated SSID (max 32 chars) */
    int8_t rssi;         /* signal strength in dBm */
    char auth_mode[12];  /* "OPEN", "WPA2", "WPA3", etc. */
} wifi_scan_result_t;

/*
 * Initialise Wi-Fi stack (via ESP-Hosted).
 * Returns 0 on success, -1 if ESP-Hosted is not available.
 */
int wifi_module_init(void);

/*
 * Scan for available networks.
 *  results   — caller-allocated array of wifi_scan_result_t
 *  max       — capacity of results[]
 *  count_out — set to number of networks found
 * Returns 0 on success, -1 on error.
 */
int wifi_module_scan(wifi_scan_result_t *results, int max, int *count_out);

/*
 * Connect to an AP.  Blocks until connected or timeout (~10 s).
 *  ip_buf / ip_buf_len — filled with the assigned IP address string on success
 * Returns 0 on success, -1 on error.
 */
int wifi_module_connect(const char *ssid, const char *password,
                        char *ip_buf, int ip_buf_len);

/* Disconnect from the current AP. */
void wifi_module_disconnect(void);

/*
 * Ping a host (IPv4 address or hostname).
 *  latency_ms_out — set to round-trip time on success
 * Returns 0 on success, -1 on error.
 */
int wifi_module_ping(const char *host, uint32_t *latency_ms_out);
