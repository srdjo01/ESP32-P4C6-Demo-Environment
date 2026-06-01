#include "protocol.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tusb_cdc_acm.h"

#include "gpio_module.h"
#include "uart_module.h"
#include "i2c_module.h"
#include "emmc_module.h"
#include "wifi_module.h"
#include "display_module.h"

static const char *TAG = "proto";

/* ── CDC send helper ───────────────────────────────────────────────────── */

void proto_send(const char *json_str)
{
    size_t len = strlen(json_str);
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)json_str, len);
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)"\n", 1);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}

void proto_send_error(const char *cmd, const char *message)
{
    char buf[PROTO_RESP_MAX];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"error\",\"cmd\":\"%s\",\"message\":\"%s\"}",
             cmd ? cmd : "unknown", message);
    proto_send(buf);
}

/* ── Base-64 helpers (RFC 4648, no line wrapping) ──────────────────────── */

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_decode(const char *src, uint8_t *dst, size_t dst_max)
{
    static const int8_t tbl[256] = {
        [0 ... 255] = -1,
        ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
        ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
        ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
        ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
        ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
        ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
        ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
        ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63
    };
    size_t out = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; *src && *src != '='; src++) {
        int v = tbl[(unsigned char)*src];
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out >= dst_max) return -1;
            dst[out++] = (uint8_t)(acc >> bits);
        }
    }
    return (int)out;
}

static void b64_encode(const uint8_t *src, size_t len, char *dst)
{
    size_t i = 0, o = 0;
    for (; i + 2 < len; i += 3) {
        dst[o++] = B64_CHARS[(src[i]   >> 2)];
        dst[o++] = B64_CHARS[((src[i]  & 3)  << 4) | (src[i+1] >> 4)];
        dst[o++] = B64_CHARS[((src[i+1]& 0xf)<< 2) | (src[i+2] >> 6)];
        dst[o++] = B64_CHARS[(src[i+2] & 0x3f)];
    }
    if (i < len) {
        dst[o++] = B64_CHARS[(src[i] >> 2)];
        if (i + 1 < len) {
            dst[o++] = B64_CHARS[((src[i] & 3) << 4) | (src[i+1] >> 4)];
            dst[o++] = B64_CHARS[((src[i+1] & 0xf) << 2)];
        } else {
            dst[o++] = B64_CHARS[((src[i] & 3) << 4)];
            dst[o++] = '=';
        }
        dst[o++] = '=';
    }
    dst[o] = '\0';
}

/* ── Command handlers ──────────────────────────────────────────────────── */

static void handle_ping(void)
{
    char buf[PROTO_RESP_MAX];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"cmd\":\"ping\","
             "\"firmware\":\"ESP32-P4C6-Demo\",\"version\":\"1.0.0\","
             "\"uptime_ms\":%lld}",
             esp_timer_get_time() / 1000LL);
    proto_send(buf);
}

static void handle_gpio_set(cJSON *root)
{
    cJSON *jpin   = cJSON_GetObjectItem(root, "pin");
    cJSON *jlevel = cJSON_GetObjectItem(root, "level");
    if (!cJSON_IsNumber(jpin) || !cJSON_IsNumber(jlevel)) {
        proto_send_error("gpio_set", "missing pin or level");
        return;
    }
    int pin   = jpin->valueint;
    int level = jlevel->valueint;
    if (gpio_module_set(pin, level) != 0) {
        proto_send_error("gpio_set", "invalid pin");
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"cmd\":\"gpio_set\",\"pin\":%d,\"level\":%d}",
             pin, level);
    proto_send(buf);
}

static void handle_gpio_get(cJSON *root)
{
    cJSON *jpin = cJSON_GetObjectItem(root, "pin");
    if (!cJSON_IsNumber(jpin)) {
        proto_send_error("gpio_get", "missing pin");
        return;
    }
    int pin   = jpin->valueint;
    int level = gpio_module_get(pin);
    if (level < 0) {
        proto_send_error("gpio_get", "invalid pin");
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"cmd\":\"gpio_get\",\"pin\":%d,\"level\":%d}",
             pin, level);
    proto_send(buf);
}

static void handle_ign_ilum_get(void)
{
    int ign   = gpio_module_get_ignition();
    int ilum  = gpio_module_get_illumination();
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"cmd\":\"ign_ilum_get\","
             "\"ignition\":%d,\"illumination\":%d}",
             ign, ilum);
    proto_send(buf);
}

static void handle_uart_send(cJSON *root)
{
    cJSON *jport = cJSON_GetObjectItem(root, "port");
    cJSON *jdata = cJSON_GetObjectItem(root, "data_b64");
    if (!cJSON_IsNumber(jport) || !cJSON_IsString(jdata)) {
        proto_send_error("uart_send", "missing port or data_b64");
        return;
    }
    uint8_t buf[256];
    int len = b64_decode(jdata->valuestring, buf, sizeof(buf));
    if (len < 0) {
        proto_send_error("uart_send", "base64 decode failed");
        return;
    }
    int sent = uart_module_send(jport->valueint, buf, (size_t)len);
    if (sent < 0) {
        proto_send_error("uart_send", "invalid port");
        return;
    }
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"cmd\":\"uart_send\","
             "\"port\":%d,\"bytes_sent\":%d}",
             jport->valueint, sent);
    proto_send(resp);
}

static void handle_uart_recv(cJSON *root)
{
    cJSON *jport    = cJSON_GetObjectItem(root, "port");
    cJSON *jtimeout = cJSON_GetObjectItem(root, "timeout_ms");
    if (!cJSON_IsNumber(jport)) {
        proto_send_error("uart_recv", "missing port");
        return;
    }
    int timeout_ms = cJSON_IsNumber(jtimeout) ? jtimeout->valueint : 200;

    uint8_t raw[256];
    int len = uart_module_recv(jport->valueint, raw, sizeof(raw), timeout_ms);
    if (len < 0) {
        proto_send_error("uart_recv", "invalid port");
        return;
    }

    char b64[400];
    b64_encode(raw, (size_t)len, b64);

    char resp[PROTO_RESP_MAX];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"cmd\":\"uart_recv\","
             "\"port\":%d,\"bytes\":%d,\"data_b64\":\"%s\"}",
             jport->valueint, len, b64);
    proto_send(resp);
}

static void handle_i2c_read(cJSON *root)
{
    cJSON *jsensor = cJSON_GetObjectItem(root, "sensor");
    if (!cJSON_IsString(jsensor)) {
        proto_send_error("i2c_read", "missing sensor (\"accel\" or \"rtc\")");
        return;
    }

    char resp[PROTO_RESP_MAX];

    if (strcmp(jsensor->valuestring, "accel") == 0) {
        float x, y, z;
        if (i2c_module_read_accel(&x, &y, &z) != 0) {
            proto_send_error("i2c_read", "accel read failed");
            return;
        }
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"cmd\":\"i2c_read\",\"sensor\":\"accel\","
                 "\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}",
                 x, y, z);

    } else if (strcmp(jsensor->valuestring, "rtc") == 0) {
        rtc_time_t t;
        if (i2c_module_read_rtc(&t) != 0) {
            proto_send_error("i2c_read", "rtc read failed");
            return;
        }
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"cmd\":\"i2c_read\",\"sensor\":\"rtc\","
                 "\"year\":%d,\"month\":%d,\"day\":%d,"
                 "\"hour\":%d,\"minute\":%d,\"second\":%d}",
                 t.year + 2000, t.month, t.day,
                 t.hour, t.minute, t.second);
    } else {
        proto_send_error("i2c_read", "unknown sensor");
        return;
    }

    proto_send(resp);
}

static void handle_emmc_test(cJSON *root)
{
    cJSON *jfreq = cJSON_GetObjectItem(root, "freq_khz");
    cJSON *jsize = cJSON_GetObjectItem(root, "size_kb");
    int freq_khz = cJSON_IsNumber(jfreq) ? jfreq->valueint : 40000;
    int size_kb  = cJSON_IsNumber(jsize) ? jsize->valueint : 64;

    emmc_test_result_t result;
    emmc_module_run_test(freq_khz, size_kb, &result);

    char resp[PROTO_RESP_MAX];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"cmd\":\"emmc_test\","
             "\"freq_khz\":%d,\"size_kb\":%d,"
             "\"write_ok\":%s,\"read_ok\":%s,\"verify_ok\":%s,"
             "\"duration_ms\":%lu}",
             freq_khz, size_kb,
             result.write_ok  ? "true" : "false",
             result.read_ok   ? "true" : "false",
             result.verify_ok ? "true" : "false",
             result.duration_ms);
    proto_send(resp);
}

static void handle_wifi_scan(void)
{
    wifi_scan_result_t results[20];
    int count = 0;
    if (wifi_module_scan(results, 20, &count) != 0) {
        proto_send_error("wifi_scan", "scan failed — check ESP-Hosted setup");
        return;
    }

    /* Build JSON array manually to avoid large heap alloc */
    char resp[PROTO_RESP_MAX];
    int off = snprintf(resp, sizeof(resp),
                       "{\"status\":\"ok\",\"cmd\":\"wifi_scan\",\"networks\":[");
    for (int i = 0; i < count && off < (int)sizeof(resp) - 80; i++) {
        if (i > 0) off += snprintf(resp + off, sizeof(resp) - off, ",");
        off += snprintf(resp + off, sizeof(resp) - off,
                        "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\"}",
                        results[i].ssid, results[i].rssi, results[i].auth_mode);
    }
    off += snprintf(resp + off, sizeof(resp) - off, "]}");
    proto_send(resp);
}

static void handle_wifi_connect(cJSON *root)
{
    cJSON *jssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *jpass = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(jssid)) {
        proto_send_error("wifi_connect", "missing ssid");
        return;
    }
    const char *pass = cJSON_IsString(jpass) ? jpass->valuestring : "";

    char ip[16] = {0};
    if (wifi_module_connect(jssid->valuestring, pass, ip, sizeof(ip)) != 0) {
        proto_send_error("wifi_connect", "connection failed");
        return;
    }
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"cmd\":\"wifi_connect\","
             "\"ssid\":\"%s\",\"ip\":\"%s\"}",
             jssid->valuestring, ip);
    proto_send(resp);
}

static void handle_wifi_disconnect(void)
{
    wifi_module_disconnect();
    proto_send("{\"status\":\"ok\",\"cmd\":\"wifi_disconnect\"}");
}

static void handle_wifi_ping(cJSON *root)
{
    cJSON *jhost = cJSON_GetObjectItem(root, "host");
    if (!cJSON_IsString(jhost)) {
        proto_send_error("wifi_ping", "missing host");
        return;
    }
    uint32_t latency_ms = 0;
    if (wifi_module_ping(jhost->valuestring, &latency_ms) != 0) {
        proto_send_error("wifi_ping", "ping failed");
        return;
    }
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"cmd\":\"wifi_ping\","
             "\"host\":\"%s\",\"latency_ms\":%lu}",
             jhost->valuestring, latency_ms);
    proto_send(resp);
}

static void handle_display_pattern(void)
{
    if (display_module_show_pattern() != 0) {
        proto_send_error("display_pattern", "display not initialized");
        return;
    }
    proto_send("{\"status\":\"ok\",\"cmd\":\"display_pattern\"}");
}

static void handle_display_text(cJSON *root)
{
    cJSON *jtext = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(jtext)) {
        proto_send_error("display_text", "missing text");
        return;
    }
    if (display_module_show_text(jtext->valuestring) != 0) {
        proto_send_error("display_text", "display not initialized");
        return;
    }
    proto_send("{\"status\":\"ok\",\"cmd\":\"display_text\"}");
}

static void handle_display_clear(void)
{
    display_module_clear();
    proto_send("{\"status\":\"ok\",\"cmd\":\"display_clear\"}");
}

/* ── Main dispatcher ───────────────────────────────────────────────────── */

void proto_dispatch(const char *line, size_t len)
{
    (void)len;
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        proto_send_error(NULL, "json parse error");
        return;
    }

    cJSON *jcmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(jcmd)) {
        proto_send_error(NULL, "missing cmd field");
        cJSON_Delete(root);
        return;
    }

    const char *cmd = jcmd->valuestring;
    ESP_LOGD(TAG, "cmd: %s", cmd);

    if      (strcmp(cmd, "ping")             == 0) handle_ping();
    else if (strcmp(cmd, "gpio_set")         == 0) handle_gpio_set(root);
    else if (strcmp(cmd, "gpio_get")         == 0) handle_gpio_get(root);
    else if (strcmp(cmd, "ign_ilum_get")     == 0) handle_ign_ilum_get();
    else if (strcmp(cmd, "uart_send")        == 0) handle_uart_send(root);
    else if (strcmp(cmd, "uart_recv")        == 0) handle_uart_recv(root);
    else if (strcmp(cmd, "i2c_read")         == 0) handle_i2c_read(root);
    else if (strcmp(cmd, "emmc_test")        == 0) handle_emmc_test(root);
    else if (strcmp(cmd, "wifi_scan")        == 0) handle_wifi_scan();
    else if (strcmp(cmd, "wifi_connect")     == 0) handle_wifi_connect(root);
    else if (strcmp(cmd, "wifi_disconnect")  == 0) handle_wifi_disconnect();
    else if (strcmp(cmd, "wifi_ping")        == 0) handle_wifi_ping(root);
    else if (strcmp(cmd, "display_pattern")  == 0) handle_display_pattern();
    else if (strcmp(cmd, "display_text")     == 0) handle_display_text(root);
    else if (strcmp(cmd, "display_clear")    == 0) handle_display_clear();
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "unknown command: %s", cmd);
        proto_send_error(cmd, msg);
    }

    cJSON_Delete(root);
}
