# Module: Wi-Fi + Bluetooth (ESP32-C6 co-processor)

**Source:** `firmware_c6/` (C6 firmware), `host_tool/ui/wifi_panel.py`,
`host_tool/ui/bluetooth_panel.py`

## Overview

The ESP32-P4 has **no radio**. Wi-Fi and BLE come from the on-board **ESP32-C6**.
Rather than the SDIO ESP-Hosted path (which was incompatible with this IDF
version), the C6 runs a **custom firmware** that exposes its radios over UART
using the same newline-delimited JSON protocol as the P4.

```
Host GUI ──COM(CH340)──► ESP32-C6  (custom Wi-Fi/BLE firmware)
Host GUI ──COM(USB CDC)─► ESP32-P4 (all other peripherals)
```

The C6's UART0 is wired to the board's **CH340G USB-serial** adapter (confirmed
from SCH_Schematic_2026-05-11.pdf page 5/6: `ESP32C6_TXD`/`ESP32C6_RXD`), so the
host talks to the C6 on its own COM port — independent of the P4.

## C6 firmware

Build & flash (one-time, over the CH340 port — e.g. COM4):

```
cd firmware_c6
idf.py set-target esp32c6        # first time only
idf.py -p COM4 build flash
```

The firmware brings up Wi-Fi STA + the NimBLE host (with SW coexistence) and
listens for JSON commands on UART0 @ 115200.

## Commands (host → C6)

| Command | Parameters | Response |
|---------|-----------|----------|
| `ping` | — | `{firmware, version, uptime_ms}` |
| `wifi_scan` | — | `{networks:[{ssid, rssi, auth}]}` |
| `wifi_connect` | `ssid`, `password` | `{ssid, ip}` |
| `wifi_disconnect` | — | `{status}` |
| `wifi_ping` | `host` | `{host, latency_ms}` |
| `ble_scan` | — | `{devices:[{name, addr, rssi}]}` (3 s passive scan) |

## Host tool usage

1. Connect to the **P4** on its USB CDC port (top-left selector) as usual.
2. Connect to the **C6** on its CH340 port using the **"C6 port"** selector and
   **Connect C6** button in the top bar.
3. **Wi-Fi tab** — scan, double-click an SSID to fill it, enter password, Connect,
   then Ping (e.g. `8.8.8.8`).
4. **Bluetooth tab** — Scan for BLE devices; results list name, address, and RSSI
   colour-coded by signal strength (green = strong, red = weak).

## Notes

- Wi-Fi and BLE share the 2.4 GHz front-end; `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`
  lets both run. Heavy Wi-Fi traffic during a BLE scan may reduce scan hits.
- The C6 firmware binary is ~1.2 MB (Wi-Fi + NimBLE), so it uses a custom
  partition table (`partitions.csv`) with a 3 MB app partition on 4 MB flash.
- C6 logs share UART0 with the JSON protocol; the host ignores non-JSON lines.
