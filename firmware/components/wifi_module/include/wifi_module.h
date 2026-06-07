#pragma once

#include <stdint.h>

/*
 * wifi_module — Wi-Fi via ESP32-C6 co-processor (direct UART link)
 *
 * The ESP32-P4 has no native radio. Wi-Fi/BLE are provided by the companion
 * ESP32-C6, which the P4 drives *directly* over a UART carried on two of the
 * inter-chip SDIO wires (no CH340 / host PC in the path):
 *
 *     P4 GPIO15 (TX) ── C6 GPIO21 (RX)      [SDIO D1 wire]
 *     P4 GPIO14 (RX) ── C6 GPIO20 (TX)      [SDIO D0 wire]
 *
 * The C6 runs firmware_c6/ and answers the same newline-delimited JSON protocol
 * the host uses; each call below sends one command and parses one response.
 *
 * Setup requirement
 * ─────────────────
 *  1. Flash firmware_c6/ to the ESP32-C6 (its protocol UART must be on GPIO20/21).
 *  2. Call wifi_module_init().
 *
 * If the C6 is missing or unflashed the module degrades gracefully: every
 * function returns -1 and init logs a warning.
 */

/* One Wi-Fi scan result entry. */
typedef struct {
    char ssid[33];       /* null-terminated SSID (max 32 chars) */
    int8_t rssi;         /* signal strength in dBm */
    char auth_mode[12];  /* "OPEN", "WPA2", "WPA3", etc. */
} wifi_scan_result_t;

/* One BLE scan result entry. */
typedef struct {
    char name[33];       /* device name (may be empty) */
    char addr[18];       /* "AA:BB:CC:DD:EE:FF" */
    int8_t rssi;         /* signal strength in dBm */
} ble_scan_result_t;

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
 * Run a passive BLE scan on the C6 (~3 s).
 *  results   — caller-allocated array of ble_scan_result_t
 *  max       — capacity of results[]
 *  count_out — set to number of devices found
 * Returns 0 on success, -1 on error.
 */
int wifi_module_ble_scan(ble_scan_result_t *results, int max, int *count_out);

/*
 * Ping a host (IPv4 address or hostname).
 *  latency_ms_out — set to round-trip time on success
 * Returns 0 on success, -1 on error.
 */
int wifi_module_ping(const char *host, uint32_t *latency_ms_out);
