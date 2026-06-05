# Testing Guide — ESP32-P4C6 Host Verification Tool

## Requirements

```bash
cd host_tool
pip install -r requirements.txt   # PyQt6, pyserial
python main.py
```

---

## Connection

1. Plug the board's USB-C (hub path) into your PC.
2. In the tool's top bar, click **Refresh** to list available COM ports.
3. Select the port that corresponds to the board (look for "CDC" in device manager,
   or on macOS `/dev/tty.usbmodem*`).
4. Click **Connect**.
5. The status indicator turns green and shows the firmware version.
6. Click **Ping board** to confirm two-way communication.

---

## Tab-by-tab test procedures

### GPIO (GPIO 14–19)

No external wiring needed.

1. Open the **GPIO** tab.
2. Click **HIGH** for GPIO 14 — use a multimeter or LED on that pin to confirm 3.3 V.
3. Click **LOW** — confirm 0 V.
4. Click **Read** to read the current level back via the firmware.

### Ignition / Illumination

No external wiring for read-back test; you need a 12/24 V source to trigger HIGH.

1. Open the **Ign / Ilum** tab.
2. Enable **Auto-refresh** — both indicators should show `INACTIVE` when no voltage is applied.
3. Apply 12 V to the Ignition input connector → indicator changes to `ACTIVE`.
4. Apply 12 V to the Illumination input → indicator changes to `ACTIVE`.

### UART (CN3)

**Loopback test (simplest):**

Wire a jumper between **CN3 TX (GPIO4)** and **CN3 RX (GPIO5)** directly on the connector.

1. Open the **UART** tab, select **CN3**.
2. Type any text in the send box and click **Send**.
3. Click **Receive** — the same text should appear in the receive log.

**Arduino bridge (optional, for CN4 cross-test):**

See the wiring schematic in `docs/modules/uart.md`.

### I2C Sensors

No external wiring — sensors are on-board.

1. Open the **I2C Sensors** tab.
2. Click **Read once** (Accelerometer) — X/Y/Z values should appear in m/s².
   With the board flat, Z should be close to ±9.81 m/s².
3. Click **Read once** (RTC) — current date/time should display (clock may run
   from reset if the RTC has not been set).
4. Enable **Auto-refresh** to see live values.

### eMMC

1. Open the **eMMC** tab.
2. Select **20 MHz (default)** and click **Run test** → expect all three phases PASS.
3. Repeat at **40 MHz (high speed)** — boards #1–5 confirmed PASS at 40 MHz.
4. Try **52 MHz** to check limits.
5. Try **400 kHz** to confirm slow-mode operation.

### Wi-Fi

**Requires:** ESP-Hosted slave firmware on ESP32-C6 (see `docs/modules/wifi.md`).

1. Open the **Wi-Fi** tab.
2. Click **Scan for networks** — nearby SSIDs should appear.
3. Double-click an SSID to auto-fill, enter the password, click **Connect**.
4. On success, the IP address is shown.
5. Enter `8.8.8.8` in the Ping field and click **Ping** — latency should appear.

### Display

**Hardware requirement:** Correct FPC cable orientation (see `docs/modules/display.md`).
Board #5 requires a new FPC cable and adapter fix.

1. Open the **Display** tab.
2. Click **Show test pattern** — eight horizontal colour bars should appear.
3. Type "Hello ESP32-P4C6!" and click **Show text** — centred white text on black.
4. Click **Clear** to fill the screen black.

### USB Serial Log

No action needed — this panel shows all raw data from the board in real time.
It is useful for debugging if a command returns an unexpected error.

---

## UART test rig wiring (loopback, CN3)

```
Board CN3 connector
───────────────────
  Pin 1  GPIO4  TX  ─────┐
  Pin 2  GPIO5  RX  ─────┘  (bridge TX↔RX for loopback)
  Pin 3  GND
```

---

## CAN bus test rig wiring (skipped — requires CAN transceiver cables)

CAN is excluded from this release. Hardware and schematic will be added once
the appropriate cabling is available.
