#pragma once

/*
 * display_module — MIPI DSI round display (CO5300 controller, 466×466, RGB565)
 *
 * Hardware
 * ────────
 *  Display controller : CO5300 (component: espressif/esp_lcd_co5300)
 *  Touch controller   : CST820B at I2C address 0x15, SDA=GPIO23, SCL=GPIO22
 *  Resolution         : 466 × 466 pixels
 *  Color format       : RGB565 (16-bit)  — RGB888 has known instability issues
 *  LDO                : Channel 3 at 2500 mV must be enabled before init
 *  UI library         : LVGL v8 via espressif/esp_lvgl_port
 *
 * NOTE: The display module requires correct FPC cable orientation.
 *       Board #5 had an inverted adapter that caused cable damage.
 *       Verify the FPC connector and cable before powering the display.
 *
 * Two screens are supported:
 *  - "Pattern" — eight horizontal colour bars (LVGL Canvas)
 *  - "Text"    — centred label with arbitrary user-supplied text
 */

/*
 * Initialise LDO, MIPI DSI bus, CO5300 panel, and LVGL port.
 * Returns 0 on success, -1 on any hardware error.
 */
int display_module_init(void);

/*
 * Switch to the test-pattern screen (horizontal colour bars).
 * Returns 0 on success, -1 if not initialised.
 */
int display_module_show_pattern(void);

/*
 * Switch to the text screen and set the displayed string.
 * text must be a null-terminated UTF-8 string (max 256 bytes).
 * Returns 0 on success, -1 if not initialised.
 */
int display_module_show_text(const char *text);

/* Clear the display (fill black). */
void display_module_clear(void);
