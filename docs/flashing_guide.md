# Flashing Guide — ESP32-P4C6 Demo Firmware

## Prerequisites

| Tool | Version | Install |
|------|---------|---------|
| ESP-IDF | ≥ 5.3.0 | [docs.espressif.com/get-started](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/get-started/) |
| Python | ≥ 3.9 | via ESP-IDF installer |
| esptool.py | bundled with IDF | — |

---

## 1. Clone / copy the firmware project

The firmware lives in `firmware/` inside this repository.

```
ESP32-P4C6 Demo Environment/
└── firmware/          ← this is the ESP-IDF project root
```

---

## 2. Set the target chip

```bash
idf.py set-target esp32p4
```

Open `sdkconfig` (or run `idf.py menuconfig`) and set:

- **Component config → ESP32P4-specific → Chip revision** → `v1.3`

---

## 3. Install managed components

On first build, IDF downloads the managed components listed in `idf_component.yml`:

```bash
idf.py build
```

This automatically fetches:
- `espressif/esp_lcd_co5300`
- `lvgl/lvgl`
- `espressif/esp_lvgl_port`

---

## 4. Flash

### Option A — USB 2.0 PHY (recommended, works on all boards)

Connect the board's **USB-C** port (the one going through the USB hub / CH340G
path, **not** the direct-to-ESP32-P4 port).  A `ttyACM*` / `COMx` device
appears.

```bash
idf.py -p /dev/ttyACM0 flash monitor
# Windows:
idf.py -p COM3 flash monitor
```

### Option B — External USB-UART adapter (required for Board #5 which has a burned eFuse)

Wire your CP2102 / CH340 adapter:

| Adapter | ESP32-P4 board |
|---------|----------------|
| TX      | GPIO 38 (RX)   |
| RX      | GPIO 37 (TX)   |
| GND     | GND            |

Put the board into **bootloader mode**:
1. Hold the **BOOT** button
2. Press and release **EN/RESET**
3. Release **BOOT**

Flash:
```bash
idf.py -p /dev/ttyUSB0 --before default_reset --after no_reset flash
```

---

## 5. Verify

After flashing, open a serial monitor at **115200 baud** on UART0 (`GPIO37/38`).
You should see:

```
I (xxx) main: ESP32-P4C6 Demo Firmware v1.0.0 starting...
I (xxx) gpio_module: GPIO module ready ...
I (xxx) uart_module: UART1 ready ...
...
I (xxx) main: Ready. Listening for JSON commands on USB CDC.
```

The JSON protocol is served on the **USB CDC** port (the `ttyACM*` / `COMx`
that appears when the board's USB-C connects through the USB hub).

---

## 6. Wi-Fi (optional)

Wi-Fi requires the ESP32-C6 to run the ESP-Hosted slave firmware:

1. Flash `esp-hosted/slave/` to the ESP32-C6 (via the CH340G port on the hub).
2. Uncomment `CONFIG_ESP_HOSTED_ENABLED=y` in `sdkconfig.defaults`.
3. Re-build and re-flash the ESP32-P4 firmware.

See `docs/modules/wifi.md` for SDIO pin mapping details.
