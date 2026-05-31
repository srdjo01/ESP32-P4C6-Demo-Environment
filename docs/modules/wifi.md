# Module: Wi-Fi (via ESP32-C6)

**Source:** `firmware/components/wifi_module/`

## Overview

The ESP32-P4 has **no native radio**.  Wi-Fi is provided by the companion
**ESP32-C6** connected over an SDIO bus using Espressif's
**ESP-Hosted** framework.

Once ESP-Hosted is configured the standard `esp_wifi` API works transparently —
the firmware calls `esp_wifi_scan_start()`, `esp_wifi_connect()`, etc., exactly
as on a native Wi-Fi chip.

## Physical connection (P4 ↔ C6)

| Function | ESP32-P4 GPIO | ESP32-C6 GPIO |
|----------|--------------|--------------|
| SDIO D0  | 14           | 20           |
| SDIO D1  | 15           | 21           |
| SDIO D2  | 16           | 22           |
| SDIO D3  | 17           | 23           |
| SDIO CLK | 18           | 19           |
| SDIO CMD | 19           | 18           |

> Confirmed from the GPIO connectivity test (`WiFI + SDIO test DONE.md`).  The
> SDIO protocol has not yet been exercised with the ESP-Hosted firmware — the
> existing test only validated physical GPIO connectivity using ESPHome.

## Setup procedure

1. **Flash ESP-Hosted slave firmware to ESP32-C6** using the CH340G USB port on the board hub.
   Get the slave binary from the ESP-Hosted release page or build from source:
   ```
   https://github.com/espressif/esp-hosted
   ```

2. **Enable ESP-Hosted in the ESP32-P4 firmware**:
   In `sdkconfig.defaults`, uncomment:
   ```
   CONFIG_ESP_HOSTED_ENABLED=y
   ```
   Then add the SDIO pin mapping to match the table above.

3. Rebuild and reflash the ESP32-P4 firmware.

## Public API

```c
int  wifi_module_init(void);
int  wifi_module_scan(wifi_scan_result_t *results, int max, int *count_out);
int  wifi_module_connect(const char *ssid, const char *password,
                         char *ip_buf, int ip_buf_len);
void wifi_module_disconnect(void);
int  wifi_module_ping(const char *host, uint32_t *latency_ms_out);
```

## JSON commands

| Command | Parameters | Description |
|---------|-----------|-------------|
| `wifi_scan` | — | Returns list of visible networks |
| `wifi_connect` | `ssid`, `password` | Connect; returns assigned IP |
| `wifi_disconnect` | — | Disconnect from current AP |
| `wifi_ping` | `host` (IP or hostname) | Single ICMP ping; returns latency |
