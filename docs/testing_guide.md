# Testing Guide — Host Verification GUI

Tab-by-tab procedures for verifying every peripheral with the PyQt6 GUI. For first
setup see [`getting_started.md`](getting_started.md); for command details see
[`protocol.md`](protocol.md).

---

## Launch & connect

```powershell
.\run_gui.ps1        # Windows (from source)
```
```bash
./run_gui.sh         # macOS/Linux (from source)
```
or run the packaged app from `dist/` (built with `build_gui`).

1. Click **Refresh** to list serial ports.
2. Select the **P4 USB-CDC** port (`USB Serial Device` / `VID 303A PID 4001`) — not
   the CH340, not the JTAG port.
3. Click **Connect** → the dot turns green and the firmware version appears.
4. Click **Ping board** → confirms two-way communication.

> One connection is all you need. Wi-Fi and Bluetooth are served by the C6 *through*
> the P4 — there is no separate "C6 port" to connect.

> **Don't recognise a port?** Click **Aliases…** next to the port picker. Type a
> friendly name for each device (e.g. *"P4 CDC"*, *"C6 CH340"*) and the picker
> will show `MyName — /dev/cu.usbmodem...` everywhere a port is selectable. The
> aliases are saved per-device under `~/.esp32_p4c6_tool/config.json` (macOS/Linux)
> or `%APPDATA%/ESP32-P4C6-Tool/config.json` (Windows) and follow you across
> sessions and machines if you copy that file.

---

## Setup tab — configure ESP-IDF and Python paths

Used by the **Flash** tab to know where ESP-IDF lives. You only need to touch this
tab once after installing the toolchain.

1. Open **Setup**. The first time, it auto-detects ESP-IDF, the Python interpreter,
   and the IDF Python venv and saves them automatically.
2. If anything is missing or wrong, **Browse…** lets you point at any directory or
   interpreter manually. Saved on every edit.
3. Click **Test** → runs `idf.py --version` with the configured paths and reports
   the version, or the exact error if something's mis-wired.

The **IDF Python venv** field matters: `idf.py` and its CLI dependencies (click,
pyserial, …) live inside that venv, *not* in the bootstrap Python interpreter
field above it. Leaving the venv blank usually means the build will fail with
*"No module named 'click'"*.

---

## Flash tab — build and flash from the GUI

No more shell scripts needed. The Flash tab calls the same `idf.py` your terminal
would, with the IDF environment loaded from `export.sh` / `export.bat`.

1. Pick a **Target** (P4 only / C6 only / Both).
2. **Firmware** paths default to the auto-detected `firmware/` and `firmware_c6/`
   directories of this repo. Override via Browse if you're flashing a fork or a
   project living elsewhere.
3. **Ports**: pick the P4 flash port (USB-Serial/JTAG) and/or the C6 CH340 port.
   Aliases set on the connection bar show up here too.
4. Tick **Flash after build** for build+flash; **Monitor after flash** to leave a
   serial monitor open.
5. **Build && Flash** runs the queued jobs sequentially; stop anytime with **Stop**.

Live build output streams into the console below — same lines you'd get from
`./build_firmware.sh`. After flashing, **power-cycle the board** (unplug/replug)
so both chips boot cleanly.

---

## GPIO (pins 16–19)

No external wiring required.

1. Open the **GPIO** tab. You'll see pins **16, 17, 18, 19** only.
2. Click **HIGH** on a pin → measure 3.3 V on it (multimeter or LED).
3. Click **LOW** → measure 0 V.
4. Click **Read** → the state indicator reflects the pin level read back from the board.

> **Why not 14/15?** They carry the ESP32-C6 Wi-Fi/BLE UART link and are reserved.
> See [`hardware_overview.md`](hardware_overview.md).

---

## Ign / Ilum (12/24 V inputs)

Read-back needs no wiring; to drive them ACTIVE you need a 12/24 V source.

1. Open **Ign / Ilum** and enable **Auto-refresh** → both read `INACTIVE` with no
   voltage applied.
2. Apply 12 V to the **Ignition** input → indicator → `ACTIVE`.
3. Apply 12 V to the **Illumination** input → indicator → `ACTIVE`.

---

## UART (CN3 / CN4)

### CN3 loopback (simplest, 3.3 V)
Jumper **CN3 TX (GPIO4)** to **CN3 RX (GPIO5)** on the connector.

1. Open **UART**, select **CN3**.
2. Type text → **Send**.
3. **Receive** → the same text comes back.

```
CN3 connector
  Pin 1  GPIO4  TX ──┐
  Pin 2  GPIO5  RX ──┘  jumper
  Pin 3  GND
```

### CN4 (5 V)
CN4 is 5 V logic with an on-board level-shifter — use a 5 V UART peer (or an Arduino),
**not** a bare 3.3 V device. See [`modules/uart.md`](modules/uart.md) for the
Arduino bridge wiring.

---

## I²C Sensors

On-board, no wiring.

1. Open **I2C Sensors**.
2. **Accelerometer (QMI8658A):** **Read once** → X/Y/Z in m/s²; flat on a table
   Z ≈ ±9.81.
3. **RTC (PCF85063):** **Read once** → date/time (may start from reset until set).
4. **Auto-refresh** shows live values (tilt the board to watch the accelerometer).

---

## eMMC

1. Open **eMMC**.
2. Start with **20 MHz (default)** → **Run test** → expect Write/Read+Verify = PASS.
3. Try **40 MHz** (high speed) and **52 MHz** (limit), and **400 kHz** (slow mode).
4. If it fails, see [troubleshooting](troubleshooting.md) — eMMC is board/LDO-dependent.

---

## Wi-Fi (via the ESP32-C6)

No separate connection — the P4 proxies to the C6.

1. Open **Wi-Fi** → **Scan for networks** → SSIDs appear with RSSI and auth type.
2. Double-click an SSID to fill it in, type the password, **Connect** → the assigned
   IP is shown.
3. Enter `8.8.8.8`, **Ping** → round-trip latency appears.

If the list stays empty, the status line tells you why (e.g. *"No response — is the
ESP32-C6 powered?"*) → power-cycle the board (see troubleshooting).

---

## Bluetooth (via the ESP32-C6)

1. Open **Bluetooth** → **Scan for BLE devices** (3-second passive scan).
2. Results list name, address, and RSSI, sorted strongest-first and colour-coded
   (green = strong, yellow = medium, red = weak).

---

## Display

The panel is brought up on the **first** display command (deferred from boot).

1. Open **Display** → **Show test pattern** → eight horizontal colour bars.
2. Type text → **Show text** → centred white text on black.
3. **Clear** → black screen.

⚠️ Needs a correctly-oriented FPC cable — see [`modules/display.md`](modules/display.md)
before attaching the panel. If no panel is attached, the command returns an error
(it won't hang the board).

---

## CAN (TJA1051 transceiver)

The CAN tab drives the ESP32-P4's TWAI controller through an external **TJA1051**
transceiver. See [`modules/can.md`](modules/can.md) for the full wiring; the short
version:

```
ESP32-P4 GPIO1  ──► TJA1051 TXD
ESP32-P4 GPIO2  ◄── TJA1051 RXD
ESP32-P4 GPIO3  ──► TJA1051 S/STB
ESP32-P4 GND    ─── TJA1051 GND
```

CAN_H / CAN_L go to your bus, with **120 Ω termination at each end**.

### Step 1 — controller-only self-test (no transceiver, no wiring)

Verifies the TWAI peripheral and the GPIO matrix without any external hardware.

1. Open **CAN**.
2. Pick a bitrate (default **500 kbit/s**).
3. Click **Self-test (loopback)**.
4. Expect `[self-test] PASS (controller loopback @ 500000)` in the log.

If this fails, the firmware build or the chip itself is wrong — fix this before
soldering anything.

### Step 2 — bring up the bus

1. Click **Start** at your bus's bitrate. The status line shows `started=True`.
2. (Optional) Tick **Silent mode (STB high)** to put the transceiver into RX-only
   mode. Useful for sniffing without disturbing the bus.

### Step 3 — send and receive a frame

1. Tick **Auto-poll** (default 100 ms) so received frames stream into the log.
2. In **Send frame**, fill in:
   - **ID:** hex (`0x123` or `123`).
   - **Data:** hex bytes, max 8 (`11 22 33 44`, `0xAA,0xBB`, …).
   - **Extended (29-bit)** for extended IDs; **RTR** for remote-frame requests.
3. Click **Send**. Confirmation appears as `[TX] 0x123  dlc=4  11 22 33 44`.
4. Frames arriving from the other side appear as `[RX] ...` in the same log.

### Step 4 — read counters

Click **Refresh status** to read `tx_errors`, `rx_errors`, `bus_errors`,
`arb_lost`, and the `bus_off` flag. If `bus_off` is `true`, the controller has
stopped transmitting — click **Stop** then **Start** to recover (the underlying
problem is usually missing termination or the wrong bitrate).

### Two-board test rig

Two boards (or one board + a USB-CAN adapter) on the same bitrate, sharing
GND, with 120 Ω at each end. Send on one side, watch the other's RX log fill up.

---

## USB Serial Log

Shows the raw line traffic to/from the board in real time. Leave it open while
testing other tabs to see exactly what was sent and received — invaluable when a
command returns an unexpected error.
