#pragma once

#include <stddef.h>

/*
 * JSON protocol — newline-delimited, over TinyUSB CDC.
 *
 * Every message (command or response) is one JSON object terminated by '\n'.
 *
 * Host → Board (commands):
 *   {"cmd":"<name>", ...params...}
 *
 * Board → Host (responses):
 *   {"status":"ok"|"error", "cmd":"<name>", ...data...}
 *
 * Board → Host (unsolicited push, e.g. streaming sensor data):
 *   {"event":"<name>", ...data...}
 *
 * Supported commands
 * ──────────────────
 *  ping
 *  gpio_set        pin level
 *  gpio_get        pin
 *  ign_ilum_get
 *  uart_send       port data_b64
 *  uart_recv       port timeout_ms
 *  i2c_read        sensor ("accel"|"rtc")
 *  i2c_stream_start   interval_ms
 *  i2c_stream_stop
 *  emmc_test       freq_khz  size_kb
 *  wifi_scan
 *  wifi_connect    ssid  password
 *  wifi_disconnect
 *  wifi_ping       host
 *  display_pattern
 *  display_text    text
 *  display_clear
 */

/* Maximum length of one incoming JSON line (bytes, including '\n'). */
#define PROTO_LINE_MAX 512

/* Maximum length of one outgoing JSON line (bytes, including '\n'). */
#define PROTO_RESP_MAX 1024

/*
 * Send a formatted JSON response string over USB CDC.
 * The string must already be a complete JSON object (no trailing newline needed
 * — this function appends '\n').
 */
void proto_send(const char *json_str);

/*
 * Send a simple error response for the given command name.
 */
void proto_send_error(const char *cmd, const char *message);

/*
 * Dispatch one null-terminated JSON line received from the host.
 * Called by the protocol task for every complete line.
 */
void proto_dispatch(const char *line, size_t len);
