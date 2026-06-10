# ESP32-P4C6 Demo Environment

A complete development, firmware, and test environment for the **ESP32-P4 + ESP32-C6**
demo board. It contains:

- **`firmware/`** — the ESP32-P4 application (all on-board peripherals).
- **`firmware_c6/`** — the ESP32-C6 Wi-Fi/BLE co-processor application.
- **`host_tool/`** — a cross-platform PyQt6 GUI that drives and verifies every peripheral.

If this is your **first time with the board, read [`docs/getting_started.md`](docs/getting_started.md)** —
it walks you from a blank PC to a working board in about 30 minutes.

---

## What is this board?

The board carries **two** Espressif chips that work together:

| Chip | Role |
|------|------|
| **ESP32-P4** | Main application MCU. Has no radio. Runs all on-board peripherals: GPIO, UART, I²C sensors, eMMC, MIPI-DSI display. Talks to the host PC over USB. |
| **ESP32-C6** | Wi-Fi + Bluetooth LE co-processor. The P4 drives it over a dedicated chip-to-chip UART. |

The host PC only ever talks to the **ESP32-P4**. When you ask for a Wi-Fi scan or a
BLE scan, the P4 forwards the request to the C6 over the inter-chip UART and relays
the answer back. You do **not** need a second connection for Wi-Fi/BLE.

```
                 USB-C (CDC)                inter-chip UART
   Host PC / GUI ───────────► ESP32-P4 ───────────────────► ESP32-C6
                              (peripherals)   GPIO15→C6 RX     (Wi-Fi + BLE)
                                              GPIO14←C6 TX
```

See [`docs/hardware_overview.md`](docs/hardware_overview.md) for the full pin map and
the reasoning behind this design.

---

## Quick start

> First time? Use the setup script — it installs ESP-IDF and the GUI dependencies for you.
> You can also point the GUI at an existing ESP-IDF install via the **Setup** tab and
> build / flash from the **Flash** tab — no shell scripts required after the initial
> install. See [`testing_guide.md`](docs/testing_guide.md#setup-tab--configure-esp-idf-and-python-paths).

### Windows (PowerShell)

```powershell
.\setup.ps1                                   # one-time: install ESP-IDF + GUI deps
.\build_firmware.ps1 -Target both -Flash -Port COM5 -C6Port COM4   # build + flash both chips
.\run_gui.ps1                                 # launch the GUI from source
.\build_gui.ps1                               # (optional) build a standalone .exe
```

### macOS / Linux (bash)

```bash
./setup.sh                                     # one-time: install ESP-IDF + GUI deps
./build_firmware.sh --target both --flash --port /dev/tty.usbmodemXXXX --c6-port /dev/tty.usbserialXXXX
./run_gui.sh                                   # launch the GUI from source
./build_gui.sh                                 # (optional) build a standalone .app
```

> Replace `COM5/COM4` (or the `/dev/tty.*` paths) with **your** ports — see
> [Identifying the ports](#identifying-the-com-ports) below.

---

## Project structure

```
ESP32-P4C6 Demo Environment/
├── firmware/                 ESP-IDF project for the ESP32-P4  (target: esp32p4)
│   ├── main/                 app_main.c, protocol.c (JSON dispatcher)
│   ├── components/           gpio, uart, i2c, emmc, wifi, display, can modules
│   ├── managed_components/   esp_tinyusb, esp_lcd_co5300, lvgl, ...
│   ├── partitions.csv        4 MB flash, 3 MB app partition
│   └── sdkconfig.defaults    chip + PSRAM + TinyUSB CDC config
├── firmware_c6/              ESP-IDF project for the ESP32-C6  (target: esp32c6)
│   ├── main/                 c6_main.c (Wi-Fi/BLE + UART protocol), ble_scan.c
│   └── sdkconfig.defaults    Wi-Fi + NimBLE + coexistence config
├── host_tool/                PyQt6 GUI verification tool
│   ├── main.py               entry point
│   ├── ui/                   one panel per peripheral tab (Setup, Flash, GPIO, ...)
│   ├── config/               persistent settings (port aliases, tool paths)
│   ├── protocol/             board_connection.py (serial + JSON framing), commands.py
│   └── requirements.txt      PyQt6, pyserial
├── docs/                     ← all documentation (start with getting_started.md)
├── setup.ps1 / setup.sh              one-time dependency installer
├── build_firmware.ps1 / .sh          build / flash P4 and/or C6 firmware
├── build_gui.ps1 / .sh               package the GUI as a standalone app
└── run_gui.ps1 / .sh                 launch the GUI from source
```

---

## Identifying the COM ports

When the board is plugged in over USB-C, **two** serial ports appear for the P4,
plus **one** for the C6 (through the on-board CH340G):

| Port (example) | USB ID | Belongs to | Use it for |
|----------------|--------|-----------|------------|
| `COM6` / `tty.usbmodem*` | VID `303A` PID `4001` | ESP32-P4 **TinyUSB CDC** | **The GUI / JSON protocol** |
| `COM5` / `tty.usbserial*` | VID `303A` PID `1001` | ESP32-P4 **USB-Serial/JTAG** | Flashing the P4, P4 logs |
| `COM4` / `tty.usbserial*` | VID `1A86` PID `7523` | ESP32-C6 **CH340G** | Flashing the C6, C6 logs |

- **Windows:** open *Device Manager → Ports (COM & LPT)*. The CDC port shows as
  `USB Serial Device`; the CH340 shows as `USB-SERIAL CH340`.
- **macOS/Linux:** `ls /dev/tty.usb*` (macOS) or `ls /dev/ttyACM* /dev/ttyUSB*` (Linux).

> In the GUI you select the **CDC** port (the `4001` one). You do not need to open
> the C6 port at all for normal use.

---

## The JSON protocol (in one minute)

Every message is a single JSON object terminated by `\n`, spoken over the P4's USB-CDC port.

**Host → board:**
```json
{"cmd":"ping"}
{"cmd":"gpio_set","pin":16,"level":1}
{"cmd":"i2c_read","sensor":"accel"}
{"cmd":"can_start","bitrate":500000}
{"cmd":"wifi_scan"}
{"cmd":"ble_scan"}
```

**Board → host:**
```json
{"status":"ok","cmd":"ping","firmware":"ESP32-P4C6-Demo","version":"1.0.0","uptime_ms":1234}
{"status":"error","cmd":"gpio_set","message":"invalid pin"}
```

The full command list with every parameter is in [`docs/protocol.md`](docs/protocol.md).

---

## Documentation map

| Document | What it covers |
|----------|----------------|
| [`docs/getting_started.md`](docs/getting_started.md) | **Start here.** Blank PC → working board, step by step. |
| [`docs/hardware_overview.md`](docs/hardware_overview.md) | Architecture, the P4↔C6 link, complete pin map, connectors, power. |
| [`docs/flashing_guide.md`](docs/flashing_guide.md) | Building and flashing the P4 and C6 firmware (scripts and manual). |
| [`docs/testing_guide.md`](docs/testing_guide.md) | Tab-by-tab GUI test procedures and the wiring each one needs. |
| [`docs/protocol.md`](docs/protocol.md) | Complete JSON command/response reference. |
| [`docs/troubleshooting.md`](docs/troubleshooting.md) | Every gotcha we hit, with the fix. Read this when something misbehaves. |
| `docs/modules/*.md` | Deep-dive per peripheral (GPIO, UART, I²C, eMMC, display, Wi-Fi/BLE, CAN). |

---

## Requirements

| Tool | Version | Notes |
|------|---------|-------|
| ESP-IDF | 5.4.1 | Installed by `setup.ps1` / `setup.sh` (via EIM on Windows). |
| Python | ≥ 3.9 | For the GUI and the IDF tooling. |
| Git | any | For cloning ESP-IDF / managed components. |

**Windows only:** `winget` (pre-installed on Windows 11).
**macOS only:** Homebrew.

---

## Status at a glance

| Feature | State |
|---------|-------|
| GPIO 16–19 (set/get) | ✅ Working (14/15 reserved for the C6 link) |
| Ignition / Illumination inputs | ✅ Working |
| UART CN3 / CN4 | ✅ Working |
| I²C accelerometer (QMI8658A) + RTC (PCF85063) | ✅ Working |
| CAN bus (TJA1051 on GPIO 1/2/3) | ✅ Working — controller verified via internal loopback; needs an external transceiver for a real bus |
| Wi-Fi scan / connect / ping (via C6) | ✅ Working |
| Bluetooth LE scan (via C6) | ✅ Working |
| eMMC read/write/verify | ⚠️ Hardware-dependent (LDO ch4 / board variant) |
| MIPI-DSI display | ⚠️ Needs a correctly-oriented FPC cable (see display.md) |

See [`docs/troubleshooting.md`](docs/troubleshooting.md) for details on the ⚠️ items.
