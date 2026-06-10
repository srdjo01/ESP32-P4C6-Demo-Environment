#include "can_module.h"

#include <string.h>

#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "can_module";

/* ── Wiring ─────────────────────────────────────────────────────────────── */

#define CAN_TX_GPIO   GPIO_NUM_1
#define CAN_RX_GPIO   GPIO_NUM_2
#define CAN_STB_GPIO  GPIO_NUM_3   /* TJA1051 S/STB: low = normal, high = silent */

/* ── State ──────────────────────────────────────────────────────────────── */

static bool     s_started  = false;
static bool     s_silent   = false;
static uint32_t s_bitrate  = 0;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static bool select_timing(uint32_t bitrate, twai_timing_config_t *out)
{
    /* TWAI helper macros that ship with IDF v5.4. */
    switch (bitrate) {
        case 1000000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();   *out = t; return true; }
        case  800000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_800KBITS(); *out = t; return true; }
        case  500000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS(); *out = t; return true; }
        case  250000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS(); *out = t; return true; }
        case  125000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS(); *out = t; return true; }
        case  100000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_100KBITS(); *out = t; return true; }
        case   50000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_50KBITS();  *out = t; return true; }
        case   25000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_25KBITS();  *out = t; return true; }
        default: return false;
    }
}

static void apply_stb(void)
{
    gpio_set_level(CAN_STB_GPIO, s_silent ? 1 : 0);
}

/* ── Init ───────────────────────────────────────────────────────────────── */

void can_module_init(void)
{
    /* Configure STB as output, low (normal mode) by default. */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << CAN_STB_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    s_silent = false;
    apply_stb();

    ESP_LOGI(TAG, "CAN ready  (TX=GPIO%d  RX=GPIO%d  STB=GPIO%d, normal mode)",
             CAN_TX_GPIO, CAN_RX_GPIO, CAN_STB_GPIO);
}

/* ── Start / Stop ───────────────────────────────────────────────────────── */

static int driver_install(uint32_t bitrate, twai_mode_t mode)
{
    twai_timing_config_t timing;
    if (!select_timing(bitrate, &timing)) return -1;

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, mode);
    g.tx_queue_len = 8;
    g.rx_queue_len = 16;

    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g, &timing, &f) != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install failed");
        return -2;
    }
    if (twai_start() != ESP_OK) {
        ESP_LOGE(TAG, "twai_start failed");
        twai_driver_uninstall();
        return -2;
    }
    return 0;
}

int can_module_start(uint32_t bitrate)
{
    if (s_started) {
        /* Restart with new bitrate. */
        twai_stop();
        twai_driver_uninstall();
        s_started = false;
    }

    int rc = driver_install(bitrate, TWAI_MODE_NORMAL);
    if (rc == 0) {
        s_started = true;
        s_bitrate = bitrate;
        ESP_LOGI(TAG, "TWAI started @ %lu bps", (unsigned long)bitrate);
    }
    return rc;
}

int can_module_stop(void)
{
    if (!s_started) return 0;
    twai_stop();
    twai_driver_uninstall();
    s_started = false;
    s_bitrate = 0;
    return 0;
}

/* ── Silent mode ────────────────────────────────────────────────────────── */

void can_module_set_silent(bool silent)
{
    s_silent = silent;
    apply_stb();
    ESP_LOGI(TAG, "TJA1051 STB %s (%s mode)",
             silent ? "HIGH" : "LOW", silent ? "silent" : "normal");
}

/* ── Send / Recv ────────────────────────────────────────────────────────── */

int can_module_send(const can_frame_t *frame, int timeout_ms)
{
    if (!s_started) return -1;
    if (!frame || frame->dlc > 8) return -2;

    twai_message_t msg = {0};
    msg.identifier        = frame->id;
    msg.extd              = frame->extended ? 1 : 0;
    msg.rtr               = frame->rtr      ? 1 : 0;
    msg.data_length_code  = frame->dlc;
    if (!frame->rtr) memcpy(msg.data, frame->data, frame->dlc);

    TickType_t to = (timeout_ms <= 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    return (twai_transmit(&msg, to) == ESP_OK) ? 0 : -2;
}

int can_module_recv(can_frame_t *frame, int timeout_ms)
{
    if (!s_started) return -1;
    if (!frame) return -1;

    twai_message_t msg;
    TickType_t to = (timeout_ms <= 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    esp_err_t err = twai_receive(&msg, to);
    if (err == ESP_ERR_TIMEOUT) return 0;
    if (err != ESP_OK) return -1;

    frame->id       = msg.identifier;
    frame->extended = msg.extd ? true : false;
    frame->rtr      = msg.rtr  ? true : false;
    frame->dlc      = msg.data_length_code;
    memset(frame->data, 0, sizeof(frame->data));
    if (!frame->rtr) memcpy(frame->data, msg.data, msg.data_length_code);
    return 1;
}

/* ── Status ─────────────────────────────────────────────────────────────── */

int can_module_get_status(can_module_status_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->silent  = s_silent;
    out->started = s_started;
    out->bitrate = s_bitrate;

    if (!s_started) return 0;

    twai_status_info_t info;
    if (twai_get_status_info(&info) != ESP_OK) return -1;
    out->tx_msgs    = info.msgs_to_tx;
    out->rx_msgs    = info.msgs_to_rx;
    out->tx_errors  = info.tx_error_counter;
    out->rx_errors  = info.rx_error_counter;
    out->bus_errors = info.bus_error_count;
    out->arb_lost   = info.arb_lost_count;
    out->bus_off    = (info.state == TWAI_STATE_BUS_OFF);
    return 0;
}

/* ── Self-test (loopback) ───────────────────────────────────────────────── */

int can_module_self_test(uint32_t bitrate)
{
    bool was_started = s_started;
    uint32_t prev_bitrate = s_bitrate;

    if (was_started) {
        twai_stop();
        twai_driver_uninstall();
        s_started = false;
    }

    /* NO_ACK lets the controller ack its own frames; together with self-reception
     * this makes a proper internal loopback test that needs no transceiver. */
    int rc = driver_install(bitrate, TWAI_MODE_NO_ACK);
    if (rc != 0) {
        if (was_started) (void)driver_install(prev_bitrate, TWAI_MODE_NORMAL);
        if (was_started) s_started = true, s_bitrate = prev_bitrate;
        return -1;
    }

    twai_message_t tx = {
        .identifier       = 0x123,
        .data_length_code = 4,
        .data             = {0xDE, 0xAD, 0xBE, 0xEF},
        .self             = 1,   /* loopback */
    };
    int ok = -1;
    if (twai_transmit(&tx, pdMS_TO_TICKS(50)) == ESP_OK) {
        twai_message_t rx;
        if (twai_receive(&rx, pdMS_TO_TICKS(100)) == ESP_OK &&
            rx.identifier == tx.identifier &&
            rx.data_length_code == tx.data_length_code &&
            memcmp(rx.data, tx.data, tx.data_length_code) == 0) {
            ok = 0;
        }
    }

    twai_stop();
    twai_driver_uninstall();

    if (was_started) {
        if (driver_install(prev_bitrate, TWAI_MODE_NORMAL) == 0) {
            s_started = true;
            s_bitrate = prev_bitrate;
        }
    }
    return ok;
}
