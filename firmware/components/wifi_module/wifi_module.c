#include "wifi_module.h"
#include "esp_log.h"

static const char *TAG = "wifi_module";

/*
 * Wi-Fi (and BLE) are provided by the companion ESP32-C6, which runs its own
 * custom firmware and talks to the host PC directly over its CH340 serial port.
 * The P4 is NOT involved in Wi-Fi — these stubs simply report "not available"
 * so the rest of the P4 firmware builds and runs normally.
 *
 * See firmware_c6/ for the C6 Wi-Fi/BLE firmware and docs/modules/wifi_bt.md.
 */

int wifi_module_init(void)
{
    ESP_LOGI(TAG, "Wi-Fi handled by ESP32-C6 over its own serial port (not P4)");
    return 0;
}

int wifi_module_scan(wifi_scan_result_t *results, int max, int *count_out)
{
    (void)results; (void)max;
    *count_out = 0;
    return -1;
}

int wifi_module_connect(const char *ssid, const char *password,
                        char *ip_buf, int ip_buf_len)
{
    (void)ssid; (void)password; (void)ip_buf; (void)ip_buf_len;
    return -1;
}

void wifi_module_disconnect(void) {}

int wifi_module_ping(const char *host, uint32_t *latency_ms_out)
{
    (void)host;
    *latency_ms_out = 0;
    return -1;
}
