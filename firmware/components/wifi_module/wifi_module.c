#include "wifi_module.h"
#include "esp_log.h"

static const char *TAG = "wifi_module";

/*
 * Wi-Fi is provided by the ESP32-C6 over SDIO via ESP-Hosted. The
 * esp_wifi_remote component transparently forwards the esp_wifi_* calls below
 * to the C6, so this is the standard esp_wifi STA flow — it just runs over the
 * SDIO link. The C6 must be flashed with the matching ESP-Hosted slave firmware.
 */

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "ping/ping_sock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_event_group = NULL;
static esp_netif_t       *s_netif       = NULL;
static bool               s_init        = false;
static bool               s_connected   = false;

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

int wifi_module_init(void)
{
    if (s_init) return 0;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_event_group = xEventGroupCreate();
    s_init = true;
    ESP_LOGI(TAG, "Wi-Fi module ready (ESP-Hosted)");
    return 0;
}

int wifi_module_scan(wifi_scan_result_t *results, int max, int *count_out)
{
    if (!s_init) return -1;
    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) return -1;

    uint16_t n = (uint16_t)max;
    wifi_ap_record_t *aps = calloc(n, sizeof(*aps));
    if (!aps) return -1;
    esp_wifi_scan_get_ap_records(&n, aps);
    *count_out = n;
    for (int i = 0; i < n; i++) {
        strlcpy(results[i].ssid, (char *)aps[i].ssid, sizeof(results[i].ssid));
        results[i].rssi = aps[i].rssi;
        const char *auth[] = {"OPEN","WEP","WPA","WPA2","WPA/WPA2","WPA3","WPA2/WPA3","WAPI","OWE"};
        strlcpy(results[i].auth_mode,
                (aps[i].authmode < 9) ? auth[aps[i].authmode] : "?",
                sizeof(results[i].auth_mode));
    }
    free(aps);
    return 0;
}

int wifi_module_connect(const char *ssid, const char *password,
                        char *ip_buf, int ip_buf_len)
{
    if (!s_init) return -1;
    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t cfg = {};
    strlcpy((char *)cfg.sta.ssid,     ssid,     sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(10000));
    if (!(bits & WIFI_CONNECTED_BIT)) return -1;

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(s_netif, &ip);
    snprintf(ip_buf, ip_buf_len, IPSTR, IP2STR(&ip.ip));
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

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_RAW };
    struct addrinfo *res  = NULL;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;

    ip_addr_t target = {};
    struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = sa->sin_addr.s_addr;
    freeaddrinfo(res);

    esp_ping_config_t ping_cfg  = ESP_PING_DEFAULT_CONFIG();
    ping_cfg.target_addr        = target;
    ping_cfg.count              = 1;
    ping_cfg.timeout_ms         = 3000;

    esp_ping_handle_t ping;
    volatile uint32_t elapsed = UINT32_MAX;
    volatile bool done        = false;

    esp_ping_callbacks_t cbs = {
        .cb_args         = NULL,
        .on_ping_success = NULL,
        .on_ping_timeout = NULL,
        .on_ping_end     = NULL,
    };
    if (esp_ping_new_session(&ping_cfg, &cbs, &ping) != ESP_OK) return -1;
    esp_ping_start(ping);

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
