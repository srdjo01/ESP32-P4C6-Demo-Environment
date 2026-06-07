#include "wifi_module.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"

static const char *TAG = "wifi_module";

/*
 * Wi-Fi/BLE live on the companion ESP32-C6. The P4 talks to it *directly* over
 * a UART running on two of the inter-chip SDIO wires (used here as a plain
 * UART, not SDIO) — no CH340 / host PC in the path:
 *
 *     P4 GPIO15 (TX) ── C6 GPIO21 (RX)      [SDIO D1 wire]
 *     P4 GPIO14 (RX) ── C6 GPIO20 (TX)      [SDIO D0 wire]
 *
 * The C6 runs firmware_c6/ and answers the same newline-delimited JSON protocol
 * the host uses, so each call below sends one command line and parses one
 * matching response line. The P4 simply proxies: host → P4 (USB CDC) → C6 (UART).
 */
#define C6_UART        UART_NUM_3
#define C6_TX_GPIO     15   /* → C6 GPIO21 (RX) [SDIO D1 wire] */
#define C6_RX_GPIO     14   /* ← C6 GPIO20 (TX) [SDIO D0 wire] */
#define C6_BAUD        115200
#define C6_BUF_SIZE    4096
#define C6_LINE_MAX    2048   /* must hold the C6's largest response (scan list) */

static SemaphoreHandle_t s_lock;   /* serialises access to the C6 link */

/* ── Low-level link helpers ─────────────────────────────────────────────── */

/* Read one newline-terminated line into buf within timeout_ms. Returns length
 * (>0) or -1 on timeout. */
static int c6_read_line(char *buf, size_t max, int timeout_ms)
{
    int pos = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        uint8_t ch;
        int n = uart_read_bytes(C6_UART, &ch, 1, pdMS_TO_TICKS(20));
        if (n <= 0) continue;
        if (ch == '\n' || ch == '\r') {
            if (pos > 0) { buf[pos] = '\0'; return pos; }
            continue;   /* skip blank lines */
        }
        if (pos < (int)max - 1) buf[pos++] = (char)ch;
    }
    return -1;
}

/*
 * Send a command line and return the first response whose "cmd" matches
 * expect_cmd, skipping any unrelated lines (e.g. the C6 "ready" event).
 * Caller owns the returned cJSON and must cJSON_Delete() it. NULL on timeout.
 */
static cJSON *c6_request(const char *cmd_json, const char *expect_cmd, int timeout_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    uart_flush_input(C6_UART);
    uart_write_bytes(C6_UART, cmd_json, strlen(cmd_json));
    uart_write_bytes(C6_UART, "\n", 1);

    cJSON *result = NULL;
    /* static (not on stack) — this runs in the small TinyUSB task; safe because
     * s_lock serialises every call into this function. */
    static char line[C6_LINE_MAX];
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (esp_timer_get_time() < deadline) {
        int remaining = (int)((deadline - esp_timer_get_time()) / 1000);
        if (remaining <= 0) break;
        if (c6_read_line(line, sizeof(line), remaining) < 0) break;

        cJSON *root = cJSON_Parse(line);
        if (!root) continue;
        cJSON *jcmd = cJSON_GetObjectItem(root, "cmd");
        if (cJSON_IsString(jcmd) && strcmp(jcmd->valuestring, expect_cmd) == 0) {
            result = root;          /* match — hand to caller */
            break;
        }
        cJSON_Delete(root);         /* event / stray line — skip */
    }

    xSemaphoreGive(s_lock);
    return result;
}

/* True if the response object carries {"status":"ok"}. */
static bool status_ok(cJSON *root)
{
    cJSON *st = cJSON_GetObjectItem(root, "status");
    return cJSON_IsString(st) && strcmp(st->valuestring, "ok") == 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int wifi_module_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();

    uart_config_t cfg = {
        .baud_rate  = C6_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(C6_UART, C6_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(C6_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(C6_UART, C6_TX_GPIO, C6_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* Probe the C6 so a missing/unflashed co-processor is obvious in the log. */
    cJSON *r = c6_request("{\"cmd\":\"ping\"}", "ping", 2000);
    if (r) {
        cJSON *fw = cJSON_GetObjectItem(r, "firmware");
        ESP_LOGI(TAG, "C6 link up on UART%d (TX=GPIO%d RX=GPIO%d): %s",
                 (int)C6_UART, C6_TX_GPIO, C6_RX_GPIO,
                 cJSON_IsString(fw) ? fw->valuestring : "?");
        cJSON_Delete(r);
        return 0;
    }

    ESP_LOGW(TAG, "C6 not responding on UART%d (TX=GPIO%d RX=GPIO%d) — "
                  "check firmware_c6 is flashed and wires 14/20, 15/21",
             (int)C6_UART, C6_TX_GPIO, C6_RX_GPIO);
    return -1;
}

int wifi_module_scan(wifi_scan_result_t *results, int max, int *count_out)
{
    *count_out = 0;

    cJSON *r = c6_request("{\"cmd\":\"wifi_scan\"}", "wifi_scan", 8000);
    if (!r) return -1;

    cJSON *nets = cJSON_GetObjectItem(r, "networks");
    if (!status_ok(r) || !cJSON_IsArray(nets)) {
        cJSON_Delete(r);
        return -1;
    }

    int n = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, nets) {
        if (n >= max) break;
        cJSON *ssid = cJSON_GetObjectItem(item, "ssid");
        cJSON *rssi = cJSON_GetObjectItem(item, "rssi");
        cJSON *auth = cJSON_GetObjectItem(item, "auth");

        if (cJSON_IsString(ssid))
            strlcpy(results[n].ssid, ssid->valuestring, sizeof(results[n].ssid));
        else
            results[n].ssid[0] = '\0';

        results[n].rssi = cJSON_IsNumber(rssi) ? (int8_t)rssi->valueint : 0;

        if (cJSON_IsString(auth))
            strlcpy(results[n].auth_mode, auth->valuestring, sizeof(results[n].auth_mode));
        else
            results[n].auth_mode[0] = '\0';

        n++;
    }
    *count_out = n;
    cJSON_Delete(r);
    return 0;
}

int wifi_module_connect(const char *ssid, const char *password,
                        char *ip_buf, int ip_buf_len)
{
    cJSON *cmd = cJSON_CreateObject();
    if (!cmd) return -1;
    cJSON_AddStringToObject(cmd, "cmd", "wifi_connect");
    cJSON_AddStringToObject(cmd, "ssid", ssid);
    cJSON_AddStringToObject(cmd, "password", password ? password : "");
    char *line = cJSON_PrintUnformatted(cmd);
    cJSON_Delete(cmd);
    if (!line) return -1;

    /* C6 waits up to ~12 s for the association; give it headroom. */
    cJSON *r = c6_request(line, "wifi_connect", 15000);
    cJSON_free(line);
    if (!r) return -1;

    int rc = -1;
    cJSON *ip = cJSON_GetObjectItem(r, "ip");
    if (status_ok(r) && cJSON_IsString(ip)) {
        strlcpy(ip_buf, ip->valuestring, ip_buf_len);
        rc = 0;
    }
    cJSON_Delete(r);
    return rc;
}

void wifi_module_disconnect(void)
{
    cJSON *r = c6_request("{\"cmd\":\"wifi_disconnect\"}", "wifi_disconnect", 3000);
    if (r) cJSON_Delete(r);
}

int wifi_module_ble_scan(ble_scan_result_t *results, int max, int *count_out)
{
    *count_out = 0;

    /* C6 runs a 3 s passive scan; allow headroom. */
    cJSON *r = c6_request("{\"cmd\":\"ble_scan\"}", "ble_scan", 6000);
    if (!r) return -1;

    cJSON *devs = cJSON_GetObjectItem(r, "devices");
    if (!status_ok(r) || !cJSON_IsArray(devs)) {
        cJSON_Delete(r);
        return -1;
    }

    int n = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, devs) {
        if (n >= max) break;
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *addr = cJSON_GetObjectItem(item, "addr");
        cJSON *rssi = cJSON_GetObjectItem(item, "rssi");

        if (cJSON_IsString(name))
            strlcpy(results[n].name, name->valuestring, sizeof(results[n].name));
        else
            results[n].name[0] = '\0';

        if (cJSON_IsString(addr))
            strlcpy(results[n].addr, addr->valuestring, sizeof(results[n].addr));
        else
            results[n].addr[0] = '\0';

        results[n].rssi = cJSON_IsNumber(rssi) ? (int8_t)rssi->valueint : 0;
        n++;
    }
    *count_out = n;
    cJSON_Delete(r);
    return 0;
}

int wifi_module_ping(const char *host, uint32_t *latency_ms_out)
{
    *latency_ms_out = 0;

    cJSON *cmd = cJSON_CreateObject();
    if (!cmd) return -1;
    cJSON_AddStringToObject(cmd, "cmd", "wifi_ping");
    cJSON_AddStringToObject(cmd, "host", host);
    char *line = cJSON_PrintUnformatted(cmd);
    cJSON_Delete(cmd);
    if (!line) return -1;

    cJSON *r = c6_request(line, "wifi_ping", 8000);
    cJSON_free(line);
    if (!r) return -1;

    int rc = -1;
    cJSON *lat = cJSON_GetObjectItem(r, "latency_ms");
    if (status_ok(r) && cJSON_IsNumber(lat)) {
        *latency_ms_out = (uint32_t)lat->valuedouble;
        rc = 0;
    }
    cJSON_Delete(r);
    return rc;
}
