# Module: Wi-Fi + Bluetooth (ESP32-C6 co-processor)

**Source:** `firmware_c6/` (C6 firmware), `firmware/components/wifi_module/`
(P4 side), `host_tool/ui/wifi_panel.py`, `host_tool/ui/bluetooth_panel.py`

## Overview

The ESP32-P4 has **no radio**. Wi-Fi and BLE come from the on-board **ESP32-C6**.
The C6 runs a **custom firmware** that exposes its radios over a UART using the
same newline-delimited JSON protocol as the P4. The P4 drives the C6 **directly**
over two of the inter-chip SDIO wires (used here as a plain UART, not SDIO), so
the host PC only ever talks to the P4:

```
Host GUI ──COM (USB CDC)──► ESP32-P4 ──UART (SDIO wires)──► ESP32-C6
                              │                               (Wi-Fi/BLE)
                              └─ proxies wifi_*/ble_scan to the C6
```

The host sends the normal `wifi_scan` / `wifi_connect` / `wifi_ping` commands to
the **P4** CDC port; the P4's `wifi_module` forwards each one to the C6 over the
UART link and returns the C6's reply. No separate C6 COM port is needed.

## Inter-chip wiring

The link runs on the SDIO D0/D1 wires between the two chips:

| Direction | P4 pin | C6 pin | SDIO wire |
|-----------|--------|--------|-----------|
| P4 → C6 | GPIO15 (TX) | GPIO21 (RX) | D1 |
| C6 → P4 | GPIO14 (RX) | GPIO20 (TX) | D0 |

Concretely:
- **P4:** `UART_NUM_3`, TX = GPIO15, RX = GPIO14, 115200 8N1
- **C6:** `UART_NUM_1`, TX = GPIO20, RX = GPIO21, 115200 8N1

The C6's **UART0** stays on the board's CH340G and now carries **console logs
only** — handy for debugging, but the protocol no longer runs there.

> GPIO14/15 on the P4 are therefore reserved for this link and have been removed
> from the `gpio_module` free-GPIO set (now 16–19). See `docs/modules/gpio.md`.

## C6 firmware

Build & flash (one-time, over the CH340 port — e.g. COM4):

```
cd firmware_c6
idf.py set-target esp32c6        # first time only
idf.py -p COM4 build flash
```

The firmware brings up Wi-Fi STA + the NimBLE host (with SW coexistence) and
listens for JSON commands on UART1 (GPIO20/21) @ 115200.

## Commands (P4 → C6)

| Command | Parameters | Response |
|---------|-----------|----------|
| `ping` | — | `{firmware, version, uptime_ms}` |
| `wifi_scan` | — | `{networks:[{ssid, rssi, auth}]}` |
| `wifi_connect` | `ssid`, `password` | `{ssid, ip}` |
| `wifi_disconnect` | — | `{status}` |
| `wifi_ping` | `host` | `{host, latency_ms}` |
| `ble_scan` | — | `{devices:[{name, addr, rssi}]}` (3 s passive scan) |

These are the same commands the host issues to the P4; the P4 proxies the
Wi-Fi/BLE ones to the C6 transparently.

## Host tool usage

1. Connect to the **P4** on its USB CDC port as usual — that is the only
   connection needed.
2. **Wi-Fi tab** — scan, double-click an SSID to fill it, enter password,
   Connect, then Ping (e.g. `8.8.8.8`). (Commands route P4 → C6 automatically.)
3. **Bluetooth tab** — Scan for BLE devices; results list name, address, and
   RSSI colour-coded by signal strength.

> Legacy note: an earlier build talked to the C6 on a separate CH340 COM port.
> The "C6 port" / "Connect C6" controls in the GUI are no longer required for
> this firmware — Wi-Fi/BLE now flow through the P4 connection.

## Notes

- Wi-Fi and BLE share the 2.4 GHz front-end; `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`
  lets both run. Heavy Wi-Fi traffic during a BLE scan may reduce scan hits.
- The C6 firmware binary is ~1.2 MB (Wi-Fi + NimBLE), so it uses a custom
  partition table (`partitions.csv`) with a 3 MB app partition on 4 MB flash.
- `wifi_module_init()` on the P4 pings the C6 at boot; if the C6 is missing or
  unflashed it logs a warning and every Wi-Fi command returns an error.
