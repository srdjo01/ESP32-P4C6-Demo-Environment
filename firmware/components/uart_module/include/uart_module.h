#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * uart_module — UART ports CN3 and CN4
 *
 * Pin map (confirmed from uart_echo Kconfig defaults for ESP32-P4)
 * ─────────────────────────────────────────────────────────────────
 *  CN3  UART_NUM_1   TX = GPIO4   RX = GPIO5   115200 8N1
 *  CN4  UART_NUM_2   TX = GPIO7   RX = GPIO6   115200 8N1  (5V via Q3/Q4 level-shifter)
 *
 * The host sends raw bytes encoded as Base-64 and receives them the same way.
 * Call uart_module_flush_rx() before sending to clear stale RX buffer data,
 * then call uart_module_recv() after sending to read the echo.
 */

/* Initialise both UART ports. Call once from app_main. */
void uart_module_init(void);

/*
 * Send `len` bytes from `data` on the given UART port (1 = CN3, 2 = CN4).
 * Returns number of bytes written, or -1 on invalid port.
 */
int uart_module_send(int port, const uint8_t *data, size_t len);

/*
 * Receive up to `max_len` bytes from the given UART port.
 * Blocks for at most timeout_ms milliseconds.
 * Returns number of bytes received (0 if nothing arrived), or -1 on invalid port.
 */
int uart_module_recv(int port, uint8_t *buf, size_t max_len, int timeout_ms);

/*
 * Flush the RX ring buffer for the given port.
 * Call this before uart_module_send() so that uart_module_recv() afterwards
 * only sees fresh data (the echo), not stale noise accumulated while idle.
 */
void uart_module_flush_rx(int port);
