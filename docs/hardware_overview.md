# Hardware Overview

This page describes how the board is wired and why, so you can reason about what
each pin does before you touch anything.

> Pin assignments below are taken from the firmware source and were cross-checked
> against the board schematic (`SCH_Schematic_2026-05-11.pdf`). Where the firmware
> is the source of truth, the relevant file is named.

---

## 1. Two-chip architecture

```
                USB-C
                  │
        ┌─────────┴──────────┐
        │     ESP32-P4       │   main MCU, no radio
        │                    │
        │  USB-CDC  ◄────────┼──── host PC / GUI  (JSON protocol)
        │  USB-JTAG ◄────────┼──── flashing + logs
        │                    │
        │  UART3  ───────────┼───────────────┐  inter-chip link
        └────────────────────┘               │
                                              ▼
                                   ┌────────────────────┐
                                   │     ESP32-C6       │  Wi-Fi + BLE
                                   │  UART1 ◄───────────┤
                                   │  UART0 ──► CH340 ──┼──► host (logs only)
                                   └────────────────────┘
```

- The **ESP32-P4** runs the application and every on-board peripheral. It has **no
  radio**.
- The **ESP32-C6** provides **Wi-Fi and Bluetooth LE**. It runs its own firmware
  (`firmware_c6/`) and is driven by the P4 over a dedicated UART.
- The host PC talks **only to the P4** over USB-CDC. Wi-Fi/BLE commands are
  proxied: `host → P4 → C6 → P4 → host`.

### Why a UART link and not SDIO?

The two chips are physically wired on **six** SDIO-capable lines. The original plan
was Espressif's ESP-Hosted (Wi-Fi over SDIO), but that path was not compatible with
the IDF version in use. Instead, two of those six wires are used as a plain
**115200-baud UART**, carrying the same newline-delimited JSON protocol the host
uses. It is simple, robust, and needs no special host drivers. (The other four
wires are currently unused and remain available for a future SDIO bring-up.)

---

## 2. The P4 ↔ C6 inter-chip link

Only **two** of the six wires are active (used as a UART):

| Direction | P4 pin | C6 pin | Original SDIO role |
|-----------|--------|--------|--------------------|
| P4 → C6 | **GPIO15** (TX) | **GPIO21** (RX) | D1 |
| C6 → P4 | **GPIO14** (RX) | **GPIO20** (TX) | D0 |

- P4 side: `UART_NUM_3`, 115200 8N1 — see `firmware/components/wifi_module/wifi_module.c`.
- C6 side: `UART_NUM_1`, 115200 8N1 — see `firmware_c6/main/c6_main.c`.
- The C6's `UART0` stays wired to the on-board **CH340G** and carries **console logs
  only** (handy for debugging; the protocol does not run there).

The full set of physical wires (for reference / future SDIO work):

| P4 | C6 | SDIO role | Status |
|----|----|-----------|--------|
| 14 | 20 | D0 | **used (C6→P4 UART RX)** |
| 15 | 21 | D1 | **used (P4→C6 UART TX)** |
| 16 | 22 | D2 | unused |
| 17 | 23 | D3 | unused |
| 18 | 19 | CLK | unused |
| 19 | 18 | CMD | unused |

> **Consequence:** because GPIO14 and GPIO15 carry the C6 link, they are **not**
> available as general-purpose test pins. The firmware reserves them and rejects
> `gpio_*` commands for 14/15. The free GPIO test set is therefore **16–19**.

---

## 3. Complete ESP32-P4 pin map

Everything the P4 firmware uses, in one place.

| Pin(s) | Function | Notes / source |
|--------|----------|----------------|
| GPIO 4 | UART CN3 **TX** | `uart_module.c`, 3.3 V |
| GPIO 5 | UART CN3 **RX** | `uart_module.c`, 3.3 V |
| GPIO 6 | UART CN4 **RX** | level-shifted from 5 V (`DP1_LV_RX`) |
| GPIO 7 | UART CN4 **TX** | level-shifted to 5 V (`DP1_LV_TX`) |
| GPIO 13 | Display **reset** | CO5300 panel reset (`display_module.c`) |
| GPIO 14 | **C6 link** UART RX | reserved — not a free GPIO |
| GPIO 15 | **C6 link** UART TX | reserved — not a free GPIO |
| GPIO 16–19 | **Free GPIO** (test) | push-pull, host-controlled (`gpio_module.c`) |
| GPIO 22 | Touch **SCL** | CST820B touch controller |
| GPIO 23 | Touch **SDA** | CST820B touch controller |
| GPIO 26 | **Illumination** input | 12/24 V via level-shifter, pull-down |
| GPIO 27 | **Ignition** input | 12/24 V via level-shifter, pull-down |
| GPIO 31 | I²C sensors **SDA** | QMI8658A + PCF85063 |
| GPIO 32 | I²C sensors **SCL** | QMI8658A + PCF85063 |
| GPIO 39 | eMMC **CMD** | 8-bit SDMMC (GPIO-matrix routed) |
| GPIO 40 | eMMC **CLK** | |
| GPIO 41 | eMMC **D6** | |
| GPIO 42 | eMMC **D5** | |
| GPIO 43 | eMMC **D3** | |
| GPIO 44 | eMMC **D4** | |
| GPIO 45 | eMMC **D0** | |
| GPIO 46 | eMMC **D1** | |
| GPIO 47 | eMMC **D2** | |
| GPIO 48 | eMMC **D7** | |
| (dedicated) | MIPI-DSI display | not routed through the GPIO matrix |

> The two SDMMC-style buses do **not** overlap: the eMMC uses 39–48, the C6 link
> uses 14/15. They can run at the same time.

---

## 4. Connectors

| Connector | Signals | Voltage | Notes |
|-----------|---------|---------|-------|
| **USB-C** | P4 USB-CDC + USB-Serial/JTAG, C6 CH340 | 5 V bus | One cable exposes all three serial ports. |
| **CN3** | UART1 TX=GPIO4, RX=GPIO5, GND | **3.3 V** | For loopback / 3.3 V UART peripherals. |
| **CN4** | UART2 (5 V logic, `DP1_RX`/`DP1_TX`) | **5 V** | On-board transistor level-shifter (Q3/Q4) converts to 3.3 V. **Do not** connect a 3.3 V device directly. |
| Ignition input | GPIO27 | 12/24 V | External level-shifter; GPIO sees 0–3.3 V. |
| Illumination input | GPIO26 | 12/24 V | External level-shifter; GPIO sees 0–3.3 V. |
| Display FPC | MIPI-DSI + touch I²C | — | ⚠️ Inverted adapter — see [`modules/display.md`](modules/display.md). |

---

## 5. Power rails (LDO channels)

The ESP32-P4's internal LDO regulator powers several rails on demand:

| LDO channel | Voltage | Powers | Enabled by |
|-------------|---------|--------|-----------|
| ch3 | 2500 mV | MIPI-DSI display | `display_module` (on first display command) |
| ch4 | 1800 mV | eMMC | `emmc_module` (around each test) |

---

## 6. USB interfaces on the P4

The P4 exposes **two** independent USB serial interfaces over the single USB-C port:

| Interface | USB ID | Purpose |
|-----------|--------|---------|
| **TinyUSB CDC-ACM** (USB 2.0 PHY) | `303A:4001` | The JSON protocol — this is the GUI port. |
| **USB-Serial/JTAG** (built-in) | `303A:1001` | Flashing the P4 and viewing its `ESP_LOG` output. |

The protocol deliberately runs on the **TinyUSB CDC** interface (not USB-Serial/JTAG),
because the latter is disabled by eFuse on some board variants.

---

## 7. Trade-off: free GPIO vs. the C6 link

Because the P4↔C6 connection only has six physical wires — the same ones exposed as
"free GPIO 14–19" — using Wi-Fi/BLE costs you **two** of those pins. The current
choice sacrifices **14/15** and keeps **16–19** free.

If you ever need 14/15 as test pins, the link can be moved to another pair (e.g.
18/19), which would instead free 14/15 and reserve those two. This requires changing
the pin defines in **both** firmwares (`wifi_module.c` on the P4, `c6_main.c` on the
C6) and the GUI's GPIO list, then reflashing both chips.
