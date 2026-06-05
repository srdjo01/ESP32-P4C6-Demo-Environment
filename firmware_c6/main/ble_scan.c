#include "ble_scan.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

static const char *TAG = "ble_scan";

/* ── Shared scan state (one scan at a time) ──────────────────────────────── */

static ble_dev_t        *s_results   = NULL;
static int               s_max       = 0;
static int               s_count     = 0;
static SemaphoreHandle_t s_done_sem  = NULL;
static bool              s_sync_done = false;

/* ── NimBLE host task ────────────────────────────────────────────────────── */

static void ble_host_task(void *param)
{
    nimble_port_run();           /* runs until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static void on_sync(void)
{
    s_sync_done = true;
    ESP_LOGI(TAG, "NimBLE host synced");
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

/* Extract a human-readable name from advertisement fields. */
static void parse_name(const struct ble_hs_adv_fields *fields, char *out, size_t out_len)
{
    if (fields->name != NULL && fields->name_len > 0) {
        size_t n = fields->name_len < out_len - 1 ? fields->name_len : out_len - 1;
        memcpy(out, fields->name, n);
        out[n] = '\0';
    } else {
        strlcpy(out, "(unknown)", out_len);
    }
}

/* GAP event callback — called for every advertisement report during a scan. */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    /* Format the address. */
    const uint8_t *a = event->disc.addr.val;  /* little-endian */
    char addr[18];
    snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
             a[5], a[4], a[3], a[2], a[1], a[0]);

    /* De-duplicate by address. Update RSSI if we've seen it before. */
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_results[i].addr, addr) == 0) {
            s_results[i].rssi = event->disc.rssi;
            return 0;
        }
    }
    if (s_count >= s_max) {
        return 0;
    }

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
        return 0;
    }

    ble_dev_t *d = &s_results[s_count];
    parse_name(&fields, d->name, sizeof(d->name));
    strlcpy(d->addr, addr, sizeof(d->addr));
    d->rssi = event->disc.rssi;
    s_count++;
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int ble_scan_init(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return -1;
    }

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    s_done_sem = xSemaphoreCreateBinary();
    if (!s_done_sem) return -1;

    nimble_port_freertos_init(ble_host_task);

    /* Wait briefly for the host to sync (controller ready). */
    for (int i = 0; i < 100 && !s_sync_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return s_sync_done ? 0 : -1;
}

int ble_scan_run(ble_dev_t *out, int max, int duration_ms)
{
    if (!s_sync_done) {
        ESP_LOGW(TAG, "BLE host not synced");
        return -1;
    }

    s_results = out;
    s_max     = max;
    s_count   = 0;

    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        ESP_LOGE(TAG, "no BLE address");
        return -1;
    }

    struct ble_gap_disc_params disc = {
        .passive          = 1,    /* passive scan — just listen, no scan req  */
        .itvl             = 0,    /* use stack defaults                       */
        .window           = 0,
        .filter_duplicates = 0,   /* we de-dup ourselves so RSSI updates      */
        .limited          = 0,
    };

    int rc = ble_gap_disc(own_addr_type, duration_ms, &disc, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return -1;
    }

    /* The scan auto-stops after duration_ms. Just wait it out (plus margin). */
    vTaskDelay(pdMS_TO_TICKS(duration_ms + 200));
    ble_gap_disc_cancel();   /* harmless if already stopped */

    return s_count;
}
