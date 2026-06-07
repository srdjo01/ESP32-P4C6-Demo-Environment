# Module: Display

**Source:** `firmware/components/display_module/`

## Overview

Controls the **466×466 MIPI DSI round display** using:

| Component | Details |
|-----------|---------|
| Display controller | CO5300 (`espressif/esp_lcd_co5300`) |
| Touch controller | CST820B (I2C addr 0x15, SDA=GPIO23, SCL=GPIO22) |
| Color format | RGB565 (16-bit) — RGB888 has known instability |
| UI library | LVGL via `espressif/esp_lvgl_port` |
| Power | LDO channel 3 at 2500 mV |
| Reset GPIO | 13 (required — panel won't init without reset pulse) |
| RGB order | BGR |
| MIPI DSI | 1 data lane @ 480 Mbps |
| Init timing | **On demand** — see below |

## ⚠ Hardware warning

The **adapter board for the display connectors has an inverted FPC connector**.
This caused cable damage on board #5 and possible MIPI DSI PHY damage.

**Before connecting the display, verify:**
1. The FPC cable type: contacts must be on the **opposite side** ("opposite-side
   flat cable") to compensate for the inverted adapter.
2. Board #5 may have a damaged DSI PHY — use another board for display testing.

## On-demand initialisation

The display is **not** initialised at boot. The CO5300's `panel_init` busy-waits on
the MIPI-DSI read FIFO; with no panel (or a mis-seated FPC) that wait never returns,
which would pin the CPU and starve the rest of the system. Instead,
`display_module_init()` runs the **first time** a `display_*` command is received
(`protocol.c` calls it before each display command; it is idempotent). If no panel
is attached, that first command returns an error cleanly — the board keeps running.

## Initialisation sequence (on first display command)

1. Enable LDO ch3 at 2500 mV
2. Create MIPI DSI bus (1 lane, 480 Mbps)
3. Create CO5300 panel via `esp_lcd_new_panel_co5300()` (reset GPIO 13, BGR order)
4. Reset and init panel (the CO5300 turns on during init; it does **not** support
   `esp_lcd_panel_disp_on_off`)
5. Init LVGL port and register the DSI display
6. Pre-create two LVGL screens: pattern and text

## Two display modes

### Test pattern

Eight horizontal colour bars (classic broadcast standard):
White — Yellow — Cyan — Green — Magenta — Red — Blue — Black

### Text mode

User-supplied UTF-8 string, centred on the round display using `lv_label`.
Font: Montserrat 14 pt (bundled with LVGL).

## Public API

```c
int  display_module_init(void);
int  display_module_show_pattern(void);
int  display_module_show_text(const char *text);
void display_module_clear(void);
```

## JSON commands

| Command | Parameters | Description |
|---------|-----------|-------------|
| `display_pattern` | — | Show colour bars |
| `display_text` | `text` (UTF-8 string) | Show centred text |
| `display_clear` | — | Fill black |
