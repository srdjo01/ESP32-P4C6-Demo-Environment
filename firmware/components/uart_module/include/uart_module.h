#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * uart_module — UART ports CN3 and CN4
 *
 * Pin map (from board test documentation)
 * ───────────────────────────────────────
 *  CN3  UART_NUM_1   RX = GPIO4   TX = GPIO5   115200 8N1
 *  CN4  UART_NUM_2   RX = GPIO6   TX = GPIO7   115200 8N1  (verify from schematic)
 *
 * The host sends raw bytes encoded as Base-64 and receives them the same way.
 * uart_recv() blocks for at most timeout_ms and returns whatever arrived.
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
