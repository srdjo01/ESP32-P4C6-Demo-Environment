# Module: CAN (TJA1051)

**Source:** `firmware/components/can_module/`

## Overview

CAN bus support via the ESP32-P4's TWAI peripheral and an external **TJA1051** transceiver. The TWAI controller drives any GPIO via the matrix, so this module pins TX/RX/STB to the lowest available GPIOs:

| Function | ESP32-P4 | TJA1051 pin | Notes |
|----------|----------|-------------|-------|
| TXD (TWAI → transceiver) | **GPIO 1** | TXD | Push-pull, 3.3 V logic. |
| RXD (transceiver → TWAI) | **GPIO 2** | RXD | Open-drain on the transceiver — needs the chip's internal pull-up to idle high. |
| S / STB (silent select) | **GPIO 3** | S (pin 8) | LOW = normal mode, HIGH = silent (TX disabled). |
| GND | GND | GND | Required common reference. |

The TJA1051's `VIO` pin is fed from 3.3 V on the board side. CAN_H / CAN_L go to your bus, with **120 Ω termination** at each end of the physical bus (you supply this on the bus, not on the transceiver chip).

```
ESP32-P4               TJA1051                 CAN bus
─────────              ───────                 ───────
GPIO1  ──► TXD                         CAN_H ──┬──┐
GPIO2  ◄── RXD              CAN_H               │ ╪ 120 Ω
GPIO3  ──► S/STB            CAN_L          120 Ω╪ │
GND    ─── GND              GND                  └─┴── CAN_L
                            VIO ── 3.3 V
                            VCC ── 5 V (if available — see datasheet)
```

Configuration: bitrate selectable from **25 kbit/s** to **1 Mbit/s** at runtime. The default after `can_module_init()` is **stopped**; the host must call `can_start` before sending or receiving.

## Initialisation

`can_module_init()` only configures the STB pin (driven LOW for normal mode). It does **not** install the TWAI driver. The driver is installed and started by `can_module_start()` — that lets the host change bitrate without rebooting and avoids holding the bus active when CAN isn't in use.

## Public API

```c
typedef struct {
    uint32_t id;          /* 11-bit standard, or 29-bit extended */
    bool     extended;    /* true → 29-bit identifier */
    bool     rtr;         /* true → remote-transmission-request */
    uint8_t  dlc;         /* 0..8 */
    uint8_t  data[8];
} can_frame_t;

void can_module_init(void);
int  can_module_start(uint32_t bitrate);            /* 25k–1M */
int  can_module_stop(void);
void can_module_set_silent(bool silent);            /* drives STB pin */
int  can_module_send(const can_frame_t *frame, int timeout_ms);
int  can_module_recv(can_frame_t *frame, int timeout_ms); /* 1 = got, 0 = timeout, -1 = stopped */
int  can_module_get_status(can_module_status_t *out);
int  can_module_self_test(uint32_t bitrate);        /* internal loopback, no transceiver needed */
```

`can_module_self_test()` puts the TWAI controller into `NO_ACK` self-loopback mode, sends a frame to itself, and verifies the receive matches. It works **without any wiring or transceiver** — useful for confirming the controller path before bringing up real hardware.

## Status counters

`can_module_get_status()` exposes the live TWAI counters:

| Field | Meaning |
|-------|---------|
| `started` / `silent` / `bitrate` | Module state. |
| `tx_msgs` / `rx_msgs` | Frames currently queued in driver buffers. |
| `tx_errors` / `rx_errors` | Standard CAN error counters (cleared by re-start). |
| `bus_errors` | Bus errors observed since start. |
| `arb_lost` | Arbitration losses (normal on a busy bus when sending). |
| `bus_off` | Node has entered **bus-off** state — `can_stop` then `can_start` again to recover. |

## JSON commands

| Command | Parameters | Response |
|---------|------------|----------|
| `can_start` | `bitrate` (default 500000) | `{status, cmd, bitrate}` |
| `can_stop`  | — | `{status, cmd}` |
| `can_silent` | `silent` (bool) | `{status, cmd, silent}` |
| `can_send` | `id`, `extended`, `rtr`, `data_b64`, `timeout_ms` | `{status, cmd, id, dlc, extended, rtr}` |
| `can_recv` | `timeout_ms` (default 200) | `{status, cmd, received, id?, dlc?, extended?, rtr?, data_b64?}` |
| `can_status` | — | `{status, cmd, started, silent, bitrate, tx_queued, rx_queued, tx_errors, rx_errors, bus_errors, arb_lost, bus_off}` |
| `can_self_test` | `bitrate` (default 500000) | `{status, cmd, bitrate, loopback_ok}` |

Frame data is **Base64-encoded** the same way as the UART module. Maximum payload is 8 bytes; longer arrays return `"data > 8 bytes"`.

## Test rig — controller-only self-test

No transceiver, no wiring. Just flash the firmware and call `can_self_test`:

```json
→ {"cmd":"can_self_test","bitrate":500000}
← {"status":"ok","cmd":"can_self_test","bitrate":500000,"loopback_ok":true}
```

This proves the TWAI peripheral, the GPIO matrix, and the build are sound.

## Test rig — two-node bus

Two boards (or one board + a USB-CAN adapter) share a common ground and a 120 Ω-terminated CAN_H / CAN_L pair:

```
Board A           Board B
────────          ────────
TJA1051 ── CAN_H ── TJA1051
        ── CAN_L ──
        ── GND   ──
```

On both nodes, `can_start` at the same bitrate, then `can_send` on one and `can_recv` (or auto-poll) on the other.

## Test rig — single-node with an Arduino + MCP2515 (for development)

```
ESP32-P4 / TJA1051             Arduino + MCP2515
────────────────────           ──────────────────
CAN_H ─────────────────────── CAN_H
CAN_L ─────────────────────── CAN_L
GND   ─────────────────────── GND
120 Ω across CAN_H / CAN_L on each end of the cable
```

Standard MCP2515 sketch at the same bitrate — frames sent on either side appear on the other.

## Common pitfalls

- **No 120 Ω termination → `tx_errors` climbs and `bus_off` trips.** Add a resistor at each end of the physical bus.
- **Bus-off state.** Once tripped, the controller refuses to TX. Recover with `can_stop` then `can_start`.
- **Silent mode + transmit = nothing happens.** Silent mode keeps the receiver alive but the transceiver won't drive the bus. Toggle the checkbox off to transmit.
- **Wrong bitrate** is the most common reason a clean wire still doesn't pass frames — check the other side first.
