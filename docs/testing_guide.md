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

## USB Serial Log

Shows the raw line traffic to/from the board in real time. Leave it open while
testing other tabs to see exactly what was sent and received — invaluable when a
command returns an unexpected error.

---

## Not yet covered

- **CAN / TWAI** — excluded from this release (needs a transceiver and second node;
  wiring will be added when cabling is available).
