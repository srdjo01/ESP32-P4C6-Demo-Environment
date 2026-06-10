# Getting Started — from a blank PC to a working board

This guide takes you from nothing to a fully flashed board with a working GUI in
about 30 minutes (most of which is ESP-IDF downloading). No prior ESP32
experience is assumed.

If you only want to **use** an already-flashed board with the GUI, jump to
[Step 5](#step-5--run-the-gui).

---

## What you need

- The ESP32-P4C6 demo board and a **USB-C cable** (data-capable, not charge-only).
- A PC running **Windows 11** or **macOS**.
- Internet access (ESP-IDF is ~2 GB on first install).

---

## Step 0 — Understand the two chips (30 seconds)

The board has two processors:

- **ESP32-P4** — the main chip. Runs everything except radio. Talks to your PC over USB.
- **ESP32-C6** — Wi-Fi + Bluetooth. The P4 talks to it over an internal wire; you
  never talk to it directly during normal use.

So there are **two firmwares** to flash (one per chip) and **one GUI** that talks
to the P4. That's the whole mental model.

---

## Step 1 — Install the toolchain

This installs ESP-IDF v5.4.1 (the SDK + compiler) and the Python environment the
GUI needs.

### Windows
```powershell
cd "<path to>\ESP32-P4C6 Demo Environment"
.\setup.ps1
```
The first run downloads ESP-IDF via Espressif's installer (EIM). Expect 15–30 min.
It also creates the GUI's Python virtual environment under `host_tool\.venv`.

### macOS / Linux
```bash
cd "<path to>/ESP32-P4C6 Demo Environment"
./setup.sh
```

> **Already have ESP-IDF?** The build scripts expect **v5.4.1**. On Windows they
> look in `C:\Espressif\v5.4.1`; on macOS/Linux in `~/.espressif/v5.4.1`,
> `~/esp/esp-idf`, or `/opt/esp-idf`. If yours is elsewhere, edit the path near the
> top of `build_firmware.ps1` / `build_firmware.sh`.

---

## Step 2 — Plug in the board and find your ports

Plug the board into your PC with the USB-C cable. Three serial ports should appear.

### Windows
Open **Device Manager → Ports (COM & LPT)**:

| What you see | Which chip | You will use it for |
|--------------|-----------|---------------------|
| `USB Serial Device (COMx)` | ESP32-P4 USB-CDC | **the GUI** |
| `USB JTAG/serial debug unit (COMy)` | ESP32-P4 USB-Serial/JTAG | flashing the P4 |
| `USB-SERIAL CH340 (COMz)` | ESP32-C6 | flashing the C6 |

To be 100% sure which is which, right-click each → *Properties → Details →
Hardware Ids*:
- `VID_303A&PID_4001` → P4 CDC (the GUI port)
- `VID_303A&PID_1001` → P4 USB-Serial/JTAG (P4 flash port)
- `VID_1A86&PID_7523` → C6 CH340 (C6 flash port)

### macOS / Linux
```bash
ls /dev/tty.usb*      # macOS:  tty.usbmodemXXXX (P4) and tty.usbserialXXXX (C6)
ls /dev/ttyACM* /dev/ttyUSB*   # Linux
```

Write down the three port names — you need them in the next step.

---

## Step 3 — Flash both chips

The P4 firmware (`firmware/`) and C6 firmware (`firmware_c6/`) are flashed over
**different** ports. You can do this from the GUI's **Flash** tab (recommended
for first-timers — see [Step 5](#step-5--run-the-gui)) or from the shell with
the build scripts below.

### Windows
```powershell
# Replace COM5 (P4 flash port) and COM4 (C6 CH340 port) with yours.
.\build_firmware.ps1 -Target both -Flash -Port COM5 -C6Port COM4
```

### macOS / Linux
```bash
./build_firmware.sh --target both --flash \
    --port /dev/tty.usbmodemXXXX \
    --c6-port /dev/tty.usbserialXXXX
```

You should see, twice (once per chip):
```
=== ESP32-P4 ... ===
ESP32-P4 build successful.
ESP32-P4 flash successful.
=== ESP32-C6 ... ===
ESP32-C6 build successful.
ESP32-C6 flash successful.
```

> **Flash one chip at a time** if you prefer: `-Target p4` or `-Target c6`
> (`--target p4` / `--target c6`).
>
> The very first build downloads managed components (LVGL, the display driver,
> TinyUSB) — that's normal and only happens once.

After flashing, **power-cycle the board** (unplug and replug the USB-C). This gives
both chips a clean boot. (See *Troubleshooting → "C6 not responding"* for why this
matters.)

---

## Step 4 — Confirm the P4 is alive (optional sanity check)

Open a serial monitor on the **P4 USB-Serial/JTAG** port (the `1001` one) at
115200 baud, or just watch the GUI in the next step. On boot the P4 prints:
```
I (...) main: ESP32-P4C6 Demo Firmware v1.0.0 starting...
I (...) gpio_module: GPIO module ready (free: 16-19, ...)
I (...) main: Ready. Listening for JSON commands on USB CDC.
```

---

## Step 5 — Run the GUI

### From source (recommended while developing)

**Windows**
```powershell
.\run_gui.ps1
```
**macOS / Linux**
```bash
./run_gui.sh
```

### As a standalone app (no Python needed to run it)
```powershell
.\build_gui.ps1            # Windows → dist\ESP32-P4C6-Tool.exe
```
```bash
./build_gui.sh            # macOS  → dist/ESP32-P4C6-Tool.app
```
Then launch `dist\ESP32-P4C6-Tool.exe` (or the `.app`).

> If you rebuild the `.exe`, **close any running copy first** — a running instance
> locks the file and the build will fail.

---

## Step 6 — Connect and run your first test

1. In the GUI top bar, click **Refresh**, then pick the **P4 CDC port** (the
   `USB Serial Device` / `4001` one — *not* the CH340).
2. Click **Connect**. The dot turns green and the status bar shows the firmware version.
3. Click **Ping board** — you should see `Ping OK — firmware ESP32-P4C6-Demo ...`.
4. Open the **GPIO** tab → click **HIGH** on GPIO 16 → measure 3.3 V on that pin →
   click **Read** to confirm.
5. Open the **Wi-Fi** tab → **Scan for networks** → nearby SSIDs appear.
6. Open the **Bluetooth** tab → **Scan for BLE devices** → nearby devices appear.
7. (Optional) Open the **CAN** tab → click **Self-test (loopback)** → expect
   `[self-test] PASS`. Verifies the TWAI controller without any external wiring.

That's it — you're up and running. For what each tab does and the wiring some tests
need, see [`testing_guide.md`](testing_guide.md).

---

## If something doesn't work

- The GUI shows no networks / no response → see
  [`troubleshooting.md`](troubleshooting.md) (most often a board power-cycle or the
  wrong COM port).
- GPIO 14 or 15 "don't work" → that's **by design**, they carry the C6 link; the
  testable GPIOs are **16–19**. See [`hardware_overview.md`](hardware_overview.md).
- A build or flash error → see [`flashing_guide.md`](flashing_guide.md) and
  [`troubleshooting.md`](troubleshooting.md).
