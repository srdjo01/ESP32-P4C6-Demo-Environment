#include "wifi_module.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_ping.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "wifi_module";

/* Event bits used for station connect / fail signalling. */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_event_group = NULL;
static esp_netif_t       *s_netif       = NULL;
static bool               s_init        = false;
static bool               s_connected   = false;

/* ── Event handlers ──────────────────────────────────────────────────────── */

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int wifi_module_init(void)
{
    /*
     * ESP32-P4 has no native Wi-Fi radio.  Wi-Fi is provided by the ESP32-C6
     * coprocessor via ESP-Hosted (SDIO).  The `esp_wifi` API works normally
     * once ESP-Hosted is configured and the slave firmware is running on C6.
     *
     * If ESP-Hosted is not set up, esp_wifi_init() will fail and this function
     * returns -1.  All other wifi_module_* functions also return -1 gracefully.
     */
    esp_err_t ret;

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_netif_init: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(ret));
        return -1;
    }

    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) {
        ESP_LOGW(TAG, "Failed to create default Wi-Fi STA netif");
        return -1;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_init failed: %s — ESP-Hosted may not be configured",
                 esp_err_to_name(ret));
        return -1;
    }

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, on_ip_event, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_event_group = xEventGroupCreate();
    s_init = true;
    ESP_LOGI(TAG, "Wi-Fi module ready (via ESP-Hosted / ESP32-C6)");
    return 0;
}

int wifi_module_scan(wifi_scan_result_t *results, int max, int *count_out)
{
    *count_out = 0;
    if (!s_init) return -1;

    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) return -1;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return 0;

    int take = ap_count < (uint16_t)max ? ap_count : (uint16_t)max;
    wifi_ap_record_t *aps = calloc(take, sizeof(wifi_ap_record_t));
    if (!aps) return -1;

    uint16_t n = (uint16_t)take;
    esp_wifi_scan_get_ap_records(&n, aps);

    static const char *AUTH_NAMES[] = {
        "OPEN", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA3", "WPA2/WPA3", "?"
    };
    for (int i = 0; i < (int)n; i++) {
        strncpy(results[i].ssid, (char *)aps[i].ssid, 32);
        results[i].ssid[32] = '\0';
        results[i].rssi = aps[i].rssi;
        int auth = (int)aps[i].authmode;
        if (auth < 0 || auth > 6) auth = 7;
        strncpy(results[i].auth_mode, AUTH_NAMES[auth], 11);
        results[i].auth_mode[11] = '\0';
    }
    *count_out = (int)n;
    free(aps);
    return 0;
}

int wifi_module_connect(const char *ssid, const char *password,
                        char *ip_buf, int ip_buf_len)
{
    if (!s_init) return -1;

    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid,     ssid,     sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(10000));

    if (!(bits & WIFI_CONNECTED_BIT)) return -1;

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(s_netif, &ip_info);
    snprintf(ip_buf, ip_buf_len, IPSTR, IP2STR(&ip_info.ip));
    return 0;
}

void wifi_module_disconnect(void)
{
    if (s_init) esp_wifi_disconnect();
    s_connected = false;
}

int wifi_module_ping(const char *host, uint32_t *latency_ms_out)
{
    if (!s_init || !s_connected) return -1;

    /* Resolve hostname. */
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_RAW };
    struct addrinfo *res  = NULL;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;

    ip_addr_t target;
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = sa->sin_addr.s_addr;
    freeaddrinfo(res);

    esp_ping_config_t ping_cfg  = ESP_PING_DEFAULT_CONFIG();
    ping_cfg.target_addr        = target;
    ping_cfg.count              = 1;
    ping_cfg.timeout_milliseconds = 3000;

    esp_ping_handle_t ping;
    volatile uint32_t elapsed = UINT32_MAX;
    volatile bool done        = false;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = NULL,
        .on_ping_timeout = NULL,
        .on_ping_end     = NULL,
        .cb_args         = NULL,
    };
    /* Use the simple synchronous API available in ESP-IDF 5.x. */
    if (esp_ping_new_session(&ping_cfg, &cbs, &ping) != ESP_OK) return -1;
    esp_ping_start(ping);

    /* Wait up to 4 s for the single ping to complete. */
    for (int i = 0; i < 40; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        uint32_t rx = 0;
        esp_ping_get_profile(ping, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
        if (rx > 0) {
            esp_ping_get_profile(ping, ESP_PING_PROF_DURATION, &elapsed, sizeof(elapsed));
            done = true;
            break;
        }
    }
    esp_ping_delete_session(ping);

    if (!done) return -1;
    *latency_ms_out = elapsed;
    return 0;
}
