/*
 * ESP32-C6 Wi-Fi + BLE co-processor firmware
 * ==========================================
 *
 * The ESP32-P4 has no radio; the on-board ESP32-C6 provides Wi-Fi and BLE.
 * This firmware exposes those radios to the ESP32-P4 *directly* over a UART
 * running on the inter-chip SDIO wires (used here as a plain UART, not SDIO):
 *
 *     C6 GPIO20 (TX) ── P4 GPIO14 (RX)      [SDIO D0 wire]
 *     C6 GPIO21 (RX) ── P4 GPIO15 (TX)      [SDIO D1 wire]
 *
 * The same newline-delimited JSON protocol as the ESP32-P4 firmware is used, so
 * the P4 can proxy these commands. UART0 stays wired to the CH340G and is used
 * only for console logs (the host PC no longer drives the protocol directly).
 *
 * Commands (P4 → C6):
 *   {"cmd":"ping"}
 *   {"cmd":"wifi_scan"}
 *   {"cmd":"wifi_connect","ssid":"...","password":"..."}
 *   {"cmd":"wifi_ping","host":"8.8.8.8"}
 *   {"cmd":"ble_scan"}
 *
 * Responses (C6 → host) are one JSON object per line, {"status":"ok"|"error",...}.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "ping/ping_sock.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include "ble_scan.h"

static const char *TAG = "c6_main";

/* ── UART protocol ───────────────────────────────────────────────────────── */

/*
 * Protocol link to the ESP32-P4 over the inter-chip SDIO wires (plain UART).
 * UART0 is left on the CH340G for console logging.
 */
#define PROTO_UART      UART_NUM_1
#define PROTO_TX_GPIO   20   /* → P4 GPIO14 (RX)  [SDIO D0 wire] */
#define PROTO_RX_GPIO   21   /* ← P4 GPIO15 (TX)  [SDIO D1 wire] */
#define PROTO_BAUD      115200
#define LINE_MAX        512
#define RESP_MAX        2048

static void proto_send(const char *json)
{
    uart_write_bytes(PROTO_UART, json, strlen(json));
    uart_write_bytes(PROTO_UART, "\n", 1);
}

static void proto_error(const char *cmd, const char *msg)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"error\",\"cmd\":\"%s\",\"message\":\"%s\"}",
             cmd ? cmd : "unknown", msg);
    proto_send(buf);
}

/* ── Wi-Fi state ─────────────────────────────────────────────────────────── */

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_events = NULL;
static esp_netif_t       *s_sta_netif   = NULL;
static bool               s_wifi_ready  = false;
static bool               s_connected   = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_ready = true;
    ESP_LOGI(TAG, "Wi-Fi STA started");
}

/* ── Command handlers ────────────────────────────────────────────────────── */

static void handle_ping(void)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"cmd\":\"ping\",\"firmware\":\"ESP32-C6-WiFiBT\","
             "\"version\":\"1.0.0\",\"uptime_ms\":%lld}",
             esp_timer_get_time() / 1000LL);
    proto_send(buf);
}

static const char *auth_str(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    default:                        return "?";
    }
}

static void handle_wifi_scan(void)
{
    if (!s_wifi_ready) { proto_error("wifi_scan", "wifi not ready"); return; }

    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        proto_error("wifi_scan", "scan start failed");
        return;
    }

    uint16_t n = 20;
    wifi_ap_record_t aps[20];
    if (esp_wifi_scan_get_ap_records(&n, aps) != ESP_OK) {
        proto_error("wifi_scan", "get records failed");
        return;
    }

    char resp[RESP_MAX];
    int off = snprintf(resp, sizeof(resp),
                       "{\"status\":\"ok\",\"cmd\":\"wifi_scan\",\"networks\":[");
    for (int i = 0; i < n && off < (int)sizeof(resp) - 100; i++) {
        if (i > 0) off += snprintf(resp + off, sizeof(resp) - off, ",");
        off += snprintf(resp + off, sizeof(resp) - off,
                        "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\"}",
                        (char *)aps[i].ssid, aps[i].rssi, auth_str(aps[i].authmode));
    }
    snprintf(resp + off, sizeof(resp) - off, "]}");
    proto_send(resp);
}

static void handle_wifi_connect(cJSON *root)
{
    if (!s_wifi_ready) { proto_error("wifi_connect", "wifi not ready"); return; }

    cJSON *jssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *jpass = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(jssid)) {
        proto_error("wifi_connect", "missing ssid");
        return;
    }
    const char *pass = cJSON_IsString(jpass) ? jpass->valuestring : "";

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid,     jssid->valuestring, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass,               sizeof(wc.sta.password));

    esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(12000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        proto_error("wifi_connect", "connection failed");
        return;
    }

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(s_sta_netif, &ip);
    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"cmd\":\"wifi_connect\",\"ssid\":\"%s\","
             "\"ip\":\"" IPSTR "\"}",
             jssid->valuestring, IP2STR(&ip.ip));
    proto_send(resp);
}

static void handle_wifi_disconnect(void)
{
    if (s_wifi_ready) esp_wifi_disconnect();
    s_connected = false;
    proto_send("{\"status\":\"ok\",\"cmd\":\"wifi_disconnect\"}");
}

/* ── Ping ────────────────────────────────────────────────────────────────── */

typedef struct { uint32_t latency_ms; bool got_reply; SemaphoreHandle_t done; } ping_ctx_t;

static void ping_on_success(esp_ping_handle_t hdl, void *args)
{
    ping_ctx_t *ctx = args;
    uint32_t elapsed = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    ctx->latency_ms = elapsed;
    ctx->got_reply  = true;
}

static void ping_on_end(esp_ping_handle_t hdl, void *args)
{
    ping_ctx_t *ctx = args;
    xSemaphoreGive(ctx->done);
}

static void handle_wifi_ping(cJSON *root)
{
    if (!s_connected) { proto_error("wifi_ping", "not connected"); return; }

    cJSON *jhost = cJSON_GetObjectItem(root, "host");
    if (!cJSON_IsString(jhost)) { proto_error("wifi_ping", "missing host"); return; }

    /* Resolve the target. */
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_RAW };
    struct addrinfo *res = NULL;
    if (getaddrinfo(jhost->valuestring, NULL, &hints, &res) != 0 || !res) {
        proto_error("wifi_ping", "dns resolve failed");
        return;
    }
    ip_addr_t target = {0};
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target), &sa->sin_addr);
    target.type = IPADDR_TYPE_V4;
    freeaddrinfo(res);

    ping_ctx_t ctx = { .latency_ms = 0, .got_reply = false,
                       .done = xSemaphoreCreateBinary() };

    esp_ping_config_t pc = ESP_PING_DEFAULT_CONFIG();
    pc.target_addr = target;
    pc.count       = 1;
    pc.timeout_ms  = 3000;

    esp_ping_callbacks_t cbs = {
        .cb_args         = &ctx,
        .on_ping_success = ping_on_success,
        .on_ping_timeout = NULL,
        .on_ping_end     = ping_on_end,
    };
    esp_ping_handle_t hdl;
    if (esp_ping_new_session(&pc, &cbs, &hdl) != ESP_OK) {
        vSemaphoreDelete(ctx.done);
        proto_error("wifi_ping", "ping session failed");
        return;
    }
    esp_ping_start(hdl);
    xSemaphoreTake(ctx.done, pdMS_TO_TICKS(5000));
    esp_ping_stop(hdl);
    esp_ping_delete_session(hdl);
    vSemaphoreDelete(ctx.done);

    if (!ctx.got_reply) {
        proto_error("wifi_ping", "no reply (timeout)");
        return;
    }
    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"cmd\":\"wifi_ping\",\"host\":\"%s\",\"latency_ms\":%lu}",
             jhost->valuestring, ctx.latency_ms);
    proto_send(resp);
}

/* ── BLE scan ────────────────────────────────────────────────────────────── */

static void handle_ble_scan(void)
{
    static ble_dev_t devs[20];
    int n = ble_scan_run(devs, 20, 3000);   /* 3-second scan */
    if (n < 0) {
        proto_error("ble_scan", "scan failed");
        return;
    }

    char resp[RESP_MAX];
    int off = snprintf(resp, sizeof(resp),
                       "{\"status\":\"ok\",\"cmd\":\"ble_scan\",\"devices\":[");
    for (int i = 0; i < n && off < (int)sizeof(resp) - 100; i++) {
        if (i > 0) off += snprintf(resp + off, sizeof(resp) - off, ",");
        off += snprintf(resp + off, sizeof(resp) - off,
                        "{\"name\":\"%s\",\"addr\":\"%s\",\"rssi\":%d}",
                        devs[i].name, devs[i].addr, devs[i].rssi);
    }
    snprintf(resp + off, sizeof(resp) - off, "]}");
    proto_send(resp);
}

/* ── Dispatcher ──────────────────────────────────────────────────────────── */

static void dispatch(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) { proto_error(NULL, "json parse error"); return; }

    cJSON *jcmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(jcmd)) {
        proto_error(NULL, "missing cmd");
        cJSON_Delete(root);
        return;
    }
    const char *cmd = jcmd->valuestring;

    if      (strcmp(cmd, "ping")         == 0) handle_ping();
    else if (strcmp(cmd, "wifi_scan")    == 0) handle_wifi_scan();
    else if (strcmp(cmd, "wifi_connect") == 0) handle_wifi_connect(root);
    else if (strcmp(cmd, "wifi_disconnect") == 0) handle_wifi_disconnect();
    else if (strcmp(cmd, "wifi_ping")    == 0) handle_wifi_ping(root);
    else if (strcmp(cmd, "ble_scan")     == 0) handle_ble_scan();
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "unknown command: %s", cmd);
        proto_error(cmd, msg);
    }
    cJSON_Delete(root);
}

/* ── UART receive loop ───────────────────────────────────────────────────── */

static void uart_task(void *arg)
{
    static char line[LINE_MAX];
    int pos = 0;
    uint8_t ch;

    proto_send("{\"event\":\"ready\",\"firmware\":\"ESP32-C6-WiFiBT\",\"version\":\"1.0.0\"}");

    while (1) {
        int len = uart_read_bytes(PROTO_UART, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;
        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                line[pos] = '\0';
                dispatch(line);
                pos = 0;
            }
        } else if (pos < LINE_MAX - 1) {
            line[pos++] = (char)ch;
        }
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

void app_main(void)
{
    /* NVS is required by Wi-Fi. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* UART driver for the JSON protocol, on the inter-chip wires to the P4.
     * UART0 stays on the CH340G for console logs. */
    uart_config_t uc = {
        .baud_rate = PROTO_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(PROTO_UART, 2048, 0, 0, NULL, 0);
    uart_param_config(PROTO_UART, &uc);
    uart_set_pin(PROTO_UART, PROTO_TX_GPIO, PROTO_RX_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    wifi_init();

    if (ble_scan_init() != 0) {
        ESP_LOGW(TAG, "BLE init failed — ble_scan will return errors");
    }

    xTaskCreate(uart_task, "uart_task", 6144, NULL, 5, NULL);
    ESP_LOGI(TAG, "ESP32-C6 Wi-Fi+BT co-processor ready");
}
