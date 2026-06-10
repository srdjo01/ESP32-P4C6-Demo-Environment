# Flashing Guide

There are **two** firmwares: the ESP32-P4 app (`firmware/`) and the ESP32-C6
Wi-Fi/BLE app (`firmware_c6/`). They are flashed over **different** USB serial
ports. The easiest path is the build scripts; the manual `idf.py` path is below for
reference.

| Firmware | Project dir | Target | Flash port (example) |
|----------|-------------|--------|----------------------|
| ESP32-P4 app | `firmware/` | `esp32p4` | USB-Serial/JTAG, `303A:1001`, e.g. COM5 |
| ESP32-C6 Wi-Fi/BLE | `firmware_c6/` | `esp32c6` | CH340, `1A86:7523`, e.g. COM4 |

See the [README port table](../README.md#identifying-the-com-ports) for how to find
these on your PC.

---

## Prerequisites

| Tool | Version | Install |
|------|---------|---------|
| ESP-IDF | **5.4.1** | `./setup.ps1` / `./setup.sh`, or [Espressif get-started](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32p4/get-started/) |
| Python | ≥ 3.9 | bundled with ESP-IDF |
| esptool | bundled with IDF | — |

---

## Option A — from the GUI (no shell needed)

1. Launch the GUI (`run_gui.sh` / `run_gui.ps1`).
2. Open the **Setup** tab. The first time, click **Auto-detect** — it finds
   ESP-IDF, the bootstrap Python interpreter, and the IDF Python venv. Use
   **Browse…** if any path is wrong; settings are saved automatically. Click
   **Test** to confirm `idf.py --version` runs cleanly.
3. Open the **Flash** tab. Pick **Target** (P4 / C6 / both), select the flash
   ports, tick **Flash after build** (and optionally **Monitor after flash**),
   then click **Build && Flash**. Live build output streams into the console.

The GUI sources `export.sh` / `export.bat` internally, so the toolchain
(riscv-elf-gcc, esptool, IDF Python helpers) all resolve without a setup shell.
The same auto-detect logic lives in the build script for headless use.

> Don't recognise a port? Click **Aliases…** to give each device a friendly
> name — the picker then shows `MyName — /dev/cu.usbmodem...` everywhere.

---

## Option B — build scripts (headless / CI)

### Flash both chips at once
**Windows**
```powershell
.\build_firmware.ps1 -Target both -Flash -Port COM5 -C6Port COM4
```
**macOS / Linux**
```bash
./build_firmware.sh --target both --flash \
    --port /dev/tty.usbmodemXXXX --c6-port /dev/tty.usbserialXXXX
```

### Flash one chip
```powershell
.\build_firmware.ps1 -Target p4 -Flash -Port COM5     # P4 only
.\build_firmware.ps1 -Target c6 -Flash -C6Port COM4   # C6 only
```
```bash
./build_firmware.sh --target p4 --flash --port <p4-port>
./build_firmware.sh --target c6 --flash --c6-port <c6-port>
```

### Build without flashing
Drop `-Flash` / `--flash`. Add `-Monitor` / `--monitor` to open a serial monitor
after flashing.

> **After flashing, power-cycle the board** (unplug/replug) so both chips boot
> cleanly. See [troubleshooting](troubleshooting.md) for why this matters for the C6.

---

## Option C — manual `idf.py`

First make the IDF environment available in your shell:
```bash
# macOS/Linux
. ~/.espressif/v5.4.1/esp-idf/export.sh
# Windows: run the "ESP-IDF 5.4.1 PowerShell" shortcut, or
#   & C:\Espressif\v5.4.1\esp-idf\export.ps1   (may require the Espressif env vars)
```

### ESP32-P4 app
```bash
cd firmware
idf.py set-target esp32p4        # first time only
idf.py build
idf.py -p <p4-port> flash monitor
```

### ESP32-C6 Wi-Fi/BLE
```bash
cd firmware_c6
idf.py set-target esp32c6        # first time only (target is also in sdkconfig.defaults)
idf.py build
idf.py -p <c6-port> flash
```

On the first build, IDF downloads the managed components declared in
`idf_component.yml` (LVGL, `esp_lcd_co5300`, `esp_tinyusb`, …). This is automatic
and happens once.

---

## Option D — external USB-UART adapter (boards that won't enter download mode)

Some board variants have eFuse quirks that block the built-in download path. Use a
CP2102/CH340 adapter on the P4's UART0:

| Adapter | ESP32-P4 |
|---------|----------|
| TX | GPIO 38 (RX) |
| RX | GPIO 37 (TX) |
| GND | GND |

Enter bootloader mode manually:
1. Hold **BOOT**.
2. Press and release **EN/RESET**.
3. Release **BOOT**.

Flash:
```bash
idf.py -p <adapter-port> --before default_reset --after no_reset flash
```

---

## Verifying a successful flash

### ESP32-P4
Open its USB-Serial/JTAG port (`303A:1001`) at 115200:
```
I (...) main: ESP32-P4C6 Demo Firmware v1.0.0 starting...
I (...) gpio_module: GPIO module ready (free: 16-19, ign: GPIO27, ilum: GPIO26)
I (...) uart_module: UART1 ready (RX=GPIO5 TX=GPIO4 @115200 baud)
I (...) main: Ready. Listening for JSON commands on USB CDC.
```
The JSON protocol itself is served on the **TinyUSB CDC** port (`303A:4001`).

### ESP32-C6
Open its CH340 port (`1A86:7523`) at 115200. Shortly after reset you should see
Wi-Fi calibration logs (`wifi: ...`) and the co-processor banner. (The protocol the
P4 uses runs on the C6's UART1, not on this CH340 log port.)

---

## Flash layout (both chips)

`partitions.csv` — 4 MB flash, single 3 MB app partition (the Wi-Fi+BLE binary on
the C6 and the LVGL+display binary on the P4 both need the room):

```
nvs       data nvs     0x9000  0x6000
phy_init  data phy     0xf000  0x1000
factory   app  factory 0x10000 0x300000
```
