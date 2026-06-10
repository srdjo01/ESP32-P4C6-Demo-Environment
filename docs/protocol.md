# JSON Protocol Reference

The host and the board communicate with **newline-delimited JSON** over the P4's
USB-CDC port at **115200 baud** (baud is nominal — it is USB CDC, so the rate does
not really matter). Every request and every response is a single JSON object on
one line, terminated by `\n`.

- **Source of truth:** `firmware/main/protocol.c` (dispatcher) and
  `host_tool/protocol/commands.py` (host-side builders).
- Wi-Fi and BLE commands are **proxied**: the P4 forwards them to the ESP32-C6 over
  the inter-chip UART and relays the reply.

---

## Envelope

**Request**
```json
{"cmd":"<command>", ...parameters}
```

**Success response**
```json
{"status":"ok","cmd":"<command>", ...fields}
```

**Error response**
```json
{"status":"error","cmd":"<command>","message":"<human-readable reason>"}
```

**Asynchronous events** (sent by the board without a request):
```json
{"event":"ready","firmware":"ESP32-P4C6-Demo","version":"1.0.0"}
{"event":"hw_ready","i2c":1,"emmc":1}
```
`ready` is emitted once the USB-CDC link is up; `hw_ready` once the slow peripherals
(I²C, eMMC, Wi-Fi link) have finished initialising in the background.

---

## System

### `ping`
Health check / identity.
```json
→ {"cmd":"ping"}
← {"status":"ok","cmd":"ping","firmware":"ESP32-P4C6-Demo","version":"1.0.0","uptime_ms":12345}
```

---

## GPIO

### `gpio_set` — drive a free GPIO
`pin` must be one of **16, 17, 18, 19**. (14/15 are reserved for the C6 link and
return `"invalid pin"`.)
```json
→ {"cmd":"gpio_set","pin":16,"level":1}
← {"status":"ok","cmd":"gpio_set","pin":16,"level":1}
```

### `gpio_get` — read a free GPIO
```json
→ {"cmd":"gpio_get","pin":16}
← {"status":"ok","cmd":"gpio_get","pin":16,"level":0}
```

### `ign_ilum_get` — read the 12/24 V inputs
```json
→ {"cmd":"ign_ilum_get"}
← {"status":"ok","cmd":"ign_ilum_get","ignition":0,"illumination":1}
```
`1` = voltage present (active), `0` = inactive.

---

## UART (CN3 / CN4)

Data is **Base64-encoded** in both directions. `port` is the connector label:
**3** = CN3 (3.3 V), **4** = CN4 (5 V).

### `uart_send`
```json
→ {"cmd":"uart_send","port":3,"data_b64":"SGVsbG8="}
← {"status":"ok","cmd":"uart_send","port":3,"bytes_sent":5}
```

### `uart_recv`
`timeout_ms` defaults to 200 if omitted.
```json
→ {"cmd":"uart_recv","port":3,"timeout_ms":500}
← {"status":"ok","cmd":"uart_recv","port":3,"bytes":5,"data_b64":"SGVsbG8="}
```

---

## I²C sensors

### `i2c_read` — accelerometer (QMI8658A, 0x6A)
```json
→ {"cmd":"i2c_read","sensor":"accel"}
← {"status":"ok","cmd":"i2c_read","sensor":"accel","x":0.10,"y":-0.05,"z":9.79}
```
Values are in m/s². Flat on a table, Z ≈ ±9.81.

### `i2c_read` — real-time clock (PCF85063ATL, 0x51)
```json
→ {"cmd":"i2c_read","sensor":"rtc"}
← {"status":"ok","cmd":"i2c_read","sensor":"rtc","year":2026,"month":6,"day":7,"hour":14,"minute":3,"second":21}
```

---

## eMMC

### `emmc_test` — write / read / verify raw sectors
Runs in the background; the response arrives when the test completes.
`freq_khz` ∈ {400, 4000, 10000, 20000, 40000, 52000} (default 40000),
`size_kb` default 64.
```json
→ {"cmd":"emmc_test","freq_khz":40000,"size_kb":64}
← {"status":"ok","cmd":"emmc_test","freq_khz":40000,"size_kb":64,
   "write_ok":true,"read_ok":true,"verify_ok":true,"duration_ms":312}
```

---

## Wi-Fi (proxied to the ESP32-C6)

### `wifi_scan`
```json
→ {"cmd":"wifi_scan"}
← {"status":"ok","cmd":"wifi_scan","networks":[
     {"ssid":"MyNet","rssi":-47,"auth":"WPA/WPA2"},
     {"ssid":"Other","rssi":-72,"auth":"WPA2"}]}
```

### `wifi_connect`
Blocks up to ~12 s while associating.
```json
→ {"cmd":"wifi_connect","ssid":"MyNet","password":"secret"}
← {"status":"ok","cmd":"wifi_connect","ssid":"MyNet","ip":"192.168.1.42"}
```

### `wifi_disconnect`
```json
→ {"cmd":"wifi_disconnect"}
← {"status":"ok","cmd":"wifi_disconnect"}
```

### `wifi_ping`
Single ICMP echo to an IP or hostname (must be connected first).
```json
→ {"cmd":"wifi_ping","host":"8.8.8.8"}
← {"status":"ok","cmd":"wifi_ping","host":"8.8.8.8","latency_ms":24}
```

---

## Bluetooth LE (proxied to the ESP32-C6)

### `ble_scan` — 3-second passive scan
```json
→ {"cmd":"ble_scan"}
← {"status":"ok","cmd":"ble_scan","devices":[
     {"name":"My Watch","addr":"AA:BB:CC:DD:EE:FF","rssi":-60},
     {"name":"","addr":"11:22:33:44:55:66","rssi":-84}]}
```

---

## Display

The display is initialised **on demand** (on the first display command), so the
first call may take a moment and will fail cleanly if no panel is attached.

### `display_pattern` — eight colour bars
```json
→ {"cmd":"display_pattern"}
← {"status":"ok","cmd":"display_pattern"}
```

### `display_text`
```json
→ {"cmd":"display_text","text":"Hello ESP32-P4C6!"}
← {"status":"ok","cmd":"display_text"}
```

### `display_clear`
```json
→ {"cmd":"display_clear"}
← {"status":"ok","cmd":"display_clear"}
```

---

## CAN (TJA1051 transceiver)

CAN bus support via TWAI. Wiring on the ESP32-P4 side:
**GPIO1 → TXD, GPIO2 ← RXD, GPIO3 → S/STB, GND ↔ GND**.
See [`modules/can.md`](modules/can.md) for the full wiring and rationale.

The bus must be **started** before sending or receiving. Frame data is
Base64-encoded the same way as `uart_send`/`uart_recv`. Max payload is 8 bytes.

### `can_start` — install the TWAI driver and start the bus
`bitrate` defaults to 500000. Supported: 25000, 50000, 100000, 125000, 250000,
500000, 800000, 1000000.
```json
→ {"cmd":"can_start","bitrate":500000}
← {"status":"ok","cmd":"can_start","bitrate":500000}
```

### `can_stop` — stop the bus and uninstall the driver
```json
→ {"cmd":"can_stop"}
← {"status":"ok","cmd":"can_stop"}
```
Use this to recover from a `bus_off` state, then `can_start` again.

### `can_silent` — toggle the TJA1051's STB pin
```json
→ {"cmd":"can_silent","silent":true}
← {"status":"ok","cmd":"can_silent","silent":true}
```
`true` = silent mode (RX only); `false` = normal mode.

### `can_send` — transmit a frame
```json
→ {"cmd":"can_send","id":291,"extended":false,"rtr":false,
   "data_b64":"ESIzRA==","timeout_ms":100}
← {"status":"ok","cmd":"can_send","id":291,"dlc":4,"extended":false,"rtr":false}
```
For RTR frames the payload is ignored; the receiver gets a remote-frame request
with the requested DLC.

### `can_recv` — receive one frame (poll)
`timeout_ms` defaults to 200. If no frame arrives, `received` is `false` and the
data fields are omitted.
```json
→ {"cmd":"can_recv","timeout_ms":200}
← {"status":"ok","cmd":"can_recv","received":true,
   "id":291,"extended":false,"rtr":false,"dlc":4,"data_b64":"ESIzRA=="}
```

### `can_status` — read driver / bus counters
```json
→ {"cmd":"can_status"}
← {"status":"ok","cmd":"can_status",
   "started":true,"silent":false,"bitrate":500000,
   "tx_queued":0,"rx_queued":0,
   "tx_errors":0,"rx_errors":0,"bus_errors":0,"arb_lost":0,"bus_off":false}
```

### `can_self_test` — internal-loopback verification
Runs the TWAI controller in `NO_ACK` self-loopback. **Needs no transceiver and
no wiring** — verifies the controller and GPIO matrix paths in isolation.
```json
→ {"cmd":"can_self_test","bitrate":500000}
← {"status":"ok","cmd":"can_self_test","bitrate":500000,"loopback_ok":true}
```

---

## Notes for tool authors

- **One command at a time.** The board processes commands serially; wait for a
  response (or timeout) before sending the next.
- **Match on `cmd`.** Responses echo the `cmd` field — use it to pair a reply with
  its request and to skip unrelated `event` lines.
- **Large responses are safe.** `wifi_scan`/`ble_scan` can exceed 1 KB; the firmware
  sends them in full (see the `proto_send` chunking in `protocol.c`). Read until you
  get a complete `\n`-terminated line before parsing.
- **Timeouts:** `ping`/`gpio_*` are instant; `wifi_scan`/`ble_scan` take ~2–4 s;
  `wifi_connect` up to ~15 s; `emmc_test` up to minutes at low clocks.
