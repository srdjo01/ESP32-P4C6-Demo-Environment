# ESP32-P4C6 Demo Environment

Development and test environment for the ESP32-P4 + ESP32-C6 demo board.

## Quick Start

### Windows
```powershell
.\setup.ps1              # Install all dependencies (ESP-IDF + GUI)
.\run_gui.ps1            # Launch the GUI tool
.\build_firmware.ps1 -Flash -Port COM5   # Build and flash firmware
.\build_gui.ps1          # Build standalone .exe
```

### macOS
```bash
./setup.sh               # Install all dependencies
./run_gui.sh             # Launch the GUI tool
./build_firmware.sh --flash --port /dev/tty.usbmodem*  # Build and flash
./build_gui.sh           # Build standalone .app
```

## Project Structure

```
ESP32-P4C6 Demo Environment/
├── firmware/                  ESP-IDF project (esp32p4c6_demo)
│   ├── main/                  app_main.c, protocol.c
│   ├── components/            gpio, uart, i2c, emmc, wifi, display modules
│   ├── managed_components/    esp_tinyusb, esp_lcd_co5300, lvgl, ...
│   └── sdkconfig.defaults     chip config (esp32p4 v1.3, PSRAM, TinyUSB CDC)
├── host_tool/                 PyQt6 GUI verification tool
│   ├── main.py                entry point
│   ├── ui/                    panels: gpio, uart, i2c, emmc, wifi, display
│   ├── protocol/              board_connection.py (serial + JSON framing)
│   └── requirements.txt       PyQt6 >= 6.6, pyserial >= 3.5
├── docs/                      flashing_guide.md, testing_guide.md
├── README.md                  this file
├── setup.ps1                  Windows: full dependency installer
├── setup.sh                   macOS/Linux: full dependency installer
├── run_gui.ps1                Windows: launch GUI
├── run_gui.sh                 macOS: launch GUI
├── build_firmware.ps1         Windows: build + optional flash
├── build_firmware.sh          macOS: build + optional flash
├── build_gui.ps1              Windows: PyInstaller → .exe
└── build_gui.sh               macOS: PyInstaller → .app
```

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| Python | >= 3.9 | For GUI and ESP-IDF tools |
| Git | any | For ESP-IDF clone |
| ESP-IDF | >= 5.3.0 | Installed via EIM (Windows) or idf_installer (macOS) |

**Windows only:** winget (pre-installed on Windows 11)  
**macOS only:** Homebrew (`/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`)

## COM Ports (Windows)

When the board is connected via USB-C, **two** COM ports appear:

| Port | Device | Purpose |
|---|---|---|
| COMx | USB Serial/JTAG (USJ) | ESP-IDF log output, debug console |
| COMy | TinyUSB CDC (VID 0x303a / PID 0x4001) | **JSON protocol — use this in the GUI** |

**How to identify the correct port:** The TinyUSB CDC port appears in Device Manager as `USB Serial Device`. Connect to this port in the GUI (Refresh → select → Connect).

### macOS / Linux
```
/dev/tty.usbmodem*   TinyUSB CDC  ← use this
/dev/tty.usbserial*  USJ or CH340
```

## Firmware Architecture

Communication: JSON newline-delimited over TinyUSB CDC-ACM (USB 2.0 PHY — **not** the internal USB Serial/JTAG).

### Boot Sequence

1. GPIO init (fast)
2. UART init (fast)
3. **USB CDC init** → board is reachable immediately
4. Sends `{"event":"ready","firmware":"ESP32-P4C6-Demo","version":"1.0.0"}`
5. Background task: I2C init, eMMC init, WiFi init (may take a few seconds)

### JSON Protocol

Every message is one JSON object terminated by `\n`.

**Host → Board (commands):**
```json
{"cmd":"ping"}
{"cmd":"gpio_set","pin":14,"level":1}
{"cmd":"gpio_get","pin":14}
{"cmd":"ign_ilum_get"}
{"cmd":"i2c_read","sensor":"accel"}
{"cmd":"i2c_read","sensor":"rtc"}
{"cmd":"emmc_test","freq_khz":20000,"size_kb":64}
{"cmd":"uart_send","port":1,"data_b64":"..."}
{"cmd":"uart_recv","port":1,"timeout_ms":500}
{"cmd":"wifi_scan"}
{"cmd":"wifi_connect","ssid":"...","password":"..."}
{"cmd":"wifi_ping","host":"8.8.8.8"}
{"cmd":"display_pattern"}
{"cmd":"display_text","text":"Hello"}
{"cmd":"display_clear"}
```

**Board → Host (responses):**
```json
{"status":"ok","cmd":"ping","firmware":"ESP32-P4C6-Demo","version":"1.0.0","uptime_ms":1234}
{"status":"error","cmd":"...","message":"..."}
```

## Known Issues & Fixes

| Issue | Status | Fix Applied |
|---|---|---|
| Display init hangs at boot if panel not connected | Fixed | Init skipped; call `display_pattern` command to init on demand |
| Accelerometer returns zeros | Fixed | Soft reset (CTRL9=0xB0) + enable accel+gyro (CTRL7=0x03) |
| eMMC timeout | Open | SDMMC card not responding — debug LDO ch4 (1.8V) activation |
| WiFi unavailable | Open | Requires ESP-Hosted slave firmware on ESP32-C6 |

## IDF v5.4.1 Compatibility Changes

The firmware was adapted from IDF v5.x API changes:

| Old | New |
|---|---|
| `esp_ldo_regulator` component | `esp_hw_support` |
| `#include "esp_ping.h"` | `#include "ping/ping_sock.h"` |
| `tinyusb_cdcacm.h` | `tusb_cdc_acm.h` |
| `TINYUSB_DEFAULT_CONFIG()` | `{}` (zero-init) |
| `tinyusb_cdcacm_init()` | `tusb_cdc_acm_init()` + `.usb_dev = TINYUSB_USBDEV_0` |
| `esp_lcd_co5300_config_t` (4th arg) | `co5300_vendor_config_t` in `.vendor_config` |
| `esp_ping` in CMakeLists | removed (part of `lwip`) |
| `wifi_module.c` direct `esp_wifi.h` | guarded with `#if defined(CONFIG_ESP_HOSTED_ENABLED)` |
| `lv_font_montserrat_24` | `lv_font_montserrat_14` |

## Test Results

| Test | Board #1 | Notes |
|---|---|---|
| GPIO 14–19 HIGH/LOW/Read | PASS | All pins respond correctly |
| Ignition input (GPIO27) | PASS | INACTIVE without 12V |
| Illumination input (GPIO26) | PASS | INACTIVE without 12V |
| I2C Accelerometer (QMI8658A 0x6A) | PASS | XYZ values, Z ≈ ±9.8 m/s² flat |
| I2C RTC (PCF85063ATL 0x51) | PASS | Clock running, needs time set |
| eMMC (8GTF4R, 8-bit SDMMC) | FAIL | Hardware timeout — debug LDO |
| UART CN3 loopback | TODO | Needs RX↔TX jumper on CN3 pin1↔pin2 |
| CAN/TWAI | TODO | Needs transceiver and second board |
| WiFi (ESP-Hosted via C6) | SKIP | Needs ESP-Hosted slave firmware on C6 |
| Display (CO5300 MIPI DSI) | SKIP | Needs correct FPC cable orientation |
