# Module: UART

**Source:** `firmware/components/uart_module/`

## Overview

Two UART ports are exposed — both verified from **SCH_Schematic_2026-05-11.pdf**:

| Label | ESP-IDF port | TX GPIO | RX GPIO | Voltage | Connector |
|-------|-------------|---------|---------|---------|-----------|
| CN3   | UART_NUM_1  | GPIO 4  | GPIO 5  | 3.3 V   | CN3 header |
| CN4   | UART_NUM_2  | GPIO 7  | GPIO 6  | **5 V** | CN4 header |

Configuration: **115200 baud, 8N1, no flow control**.

CN4 note: the connector carries 5 V logic (net names `DP1_RX`/`DP1_TX`).
An on-board transistor level-shifter (Q3/Q4) converts to 3.3 V before the GPIO
(`DP1_LV_RX` = GPIO6, `DP1_LV_TX` = GPIO7).  Do not connect a 3.3 V device
directly to the CN4 connector pins — use a 5 V UART device or add your own
level-shifter.

## Initialisation

`uart_module_init()` installs both UART drivers at boot.

## Public API

```c
void uart_module_init(void);
int  uart_module_send(int port, const uint8_t *data, size_t len);
int  uart_module_recv(int port, uint8_t *buf, size_t max_len, int timeout_ms);
```

`port` is **3** for CN3 and **4** for CN4 (the connector label, not the UART number).

## JSON commands

Data is **Base64-encoded** in both directions.

| Command | Parameters | Response |
|---------|-----------|----------|
| `uart_send` | `port` (3/4), `data_b64` (base64) | `{status, cmd, port, bytes_sent}` |
| `uart_recv` | `port` (3/4), `timeout_ms` | `{status, cmd, port, bytes, data_b64}` |

## Test rig — CN3 loopback

```
CN3 connector
─────────────
Pin 1  GPIO4  TX ──┐
Pin 2  GPIO5  RX ──┘  jumper wire
Pin 3  GND
```

Send a string → immediately receive it back.

## Test rig — CN3 ↔ Arduino bridge (full-duplex cross-test)

```
ESP32-P4 CN3          Arduino Uno
──────────────────────────────────
GPIO4 (TX)   ────→ RX (pin 0)
GPIO5 (RX)   ←──── TX (pin 1)
GND          ────── GND
```

Arduino sketch: standard `Serial.echo()` — anything received is printed back.
