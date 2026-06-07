# Module: Wi-Fi (P4 side)

**Source:** `firmware/components/wifi_module/`

> This page documents the **P4-side** module. For the complete Wi-Fi **and**
> Bluetooth picture (C6 firmware, the inter-chip link, the host GUI), see
> [`wifi_bt.md`](wifi_bt.md). For the wiring rationale see
> [`../hardware_overview.md`](../hardware_overview.md).

## Overview

The ESP32-P4 has **no radio**. Wi-Fi (and BLE) come from the companion **ESP32-C6**.
The P4's `wifi_module` is a thin client: it talks to the C6 over a direct UART and
speaks the same JSON protocol the host uses.

```
host ──USB-CDC──► P4 wifi_module ──UART3──► ESP32-C6 ──► Wi-Fi
                  (this module)   GPIO15→C6 GPIO21 (RX)
                                  GPIO14←C6 GPIO20 (TX)
```

> **Historical note:** an earlier design used Espressif ESP-Hosted (Wi-Fi over
> SDIO). That was replaced by the simpler direct-UART link. The six SDIO wires are
> still physically present; only two are used (as UART). If you find references to
> `CONFIG_ESP_HOSTED_ENABLED` anywhere, they are obsolete.

## Link parameters

| | Value |
|---|---|
| P4 UART | `UART_NUM_3`, 115200 8N1 |
| P4 TX → C6 RX | GPIO15 → C6 GPIO21 |
| P4 RX ← C6 TX | GPIO14 ← C6 GPIO20 |

Each call flushes the link, sends one JSON command, and waits (with a per-command
timeout) for the matching response, skipping any unrelated lines (e.g. the C6's
boot `ready` event). Access is serialised by a mutex, so commands from the USB task
and the boot-time probe can't interleave.

## Public API

```c
int  wifi_module_init(void);          // configures UART3, pings the C6, logs the result
int  wifi_module_scan(wifi_scan_result_t *results, int max, int *count_out);
int  wifi_module_connect(const char *ssid, const char *password,
                         char *ip_buf, int ip_buf_len);
void wifi_module_disconnect(void);
int  wifi_module_ping(const char *host, uint32_t *latency_ms_out);
int  wifi_module_ble_scan(ble_scan_result_t *results, int max, int *count_out);
```

`wifi_module_init()` returns 0 if the C6 answers a `ping` within ~2 s, otherwise -1
(it logs a warning and leaves the UART configured so later commands still work once
the C6 is up).

## JSON commands (host → P4 → C6)

| Command | Parameters | Response |
|---------|-----------|----------|
| `wifi_scan` | — | `{networks:[{ssid, rssi, auth}]}` |
| `wifi_connect` | `ssid`, `password` | `{ssid, ip}` |
| `wifi_disconnect` | — | `{status}` |
| `wifi_ping` | `host` | `{host, latency_ms}` |
| `ble_scan` | — | `{devices:[{name, addr, rssi}]}` |

See [`../protocol.md`](../protocol.md) for full request/response examples.

## Troubleshooting

Empty scans almost always mean the C6 isn't running — usually fixed by a board
power-cycle. See [`../troubleshooting.md`](../troubleshooting.md) → "Wi-Fi / Bluetooth".
