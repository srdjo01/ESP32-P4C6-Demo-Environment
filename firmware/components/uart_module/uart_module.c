#include "uart_module.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "uart_module";

/*
 * CN3 — GPIO4=TX, GPIO5=RX confirmed from uart_echo Kconfig defaults
 * (Test Examples/P4/uart_echo/main/Kconfig.projbuild: TXD=4, RXD=5).
 */
#define CN3_PORT      UART_NUM_1
#define CN3_TX_GPIO   4
#define CN3_RX_GPIO   5

/*
 * CN4 — 5 V UART with on-board transistor level-shifter (Q3/Q4).
 * The 5 V signals (DP1_RX/DP1_TX) are shifted to 3.3 V (DP1_LV_RX/DP1_LV_TX)
 * before reaching the ESP32-P4.  Verified from SCH_Schematic_2026-05-11.pdf.
 */
#define CN4_PORT      UART_NUM_2
#define CN4_RX_GPIO   6   /* DP1_LV_RX — level-shifted from CN4 pin 2 */
#define CN4_TX_GPIO   7   /* DP1_LV_TX — level-shifted to CN4 pin 3  */

#define UART_BAUD     115200
#define UART_BUF_SIZE 512

/* ── Init ───────────────────────────────────────────────────────────────── */

static void init_port(uart_port_t port, int rx, int tx)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(port, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(port, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART%d ready (RX=GPIO%d TX=GPIO%d @%d baud)",
             (int)port, rx, tx, UART_BAUD);
}

void uart_module_init(void)
{
    init_port(CN3_PORT, CN3_RX_GPIO, CN3_TX_GPIO);
    init_port(CN4_PORT, CN4_RX_GPIO, CN4_TX_GPIO);
}

/* ── Port lookup ────────────────────────────────────────────────────────── */

static uart_port_t port_num(int port)
{
    if (port == 3 || port == 1) return CN3_PORT;  /* accept both "port 3" (CN3) and UART_NUM_1 */
    if (port == 4 || port == 2) return CN4_PORT;
    return -1;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int uart_module_send(int port, const uint8_t *data, size_t len)
{
    uart_port_t p = port_num(port);
    if ((int)p < 0) return -1;
    return uart_write_bytes(p, data, len);
}

int uart_module_recv(int port, uint8_t *buf, size_t max_len, int timeout_ms)
{
    uart_port_t p = port_num(port);
    if ((int)p < 0) return -1;
    int ticks = timeout_ms / portTICK_PERIOD_MS;
    if (ticks <= 0) ticks = 1;
    return uart_read_bytes(p, buf, max_len, ticks);
}

void uart_module_flush_rx(int port)
{
    uart_port_t p = port_num(port);
    if ((int)p >= 0) uart_flush_input(p);
}
