#pragma once

/*
 * gpio_module — Free GPIO (14–19) and Ignition/Illumination inputs (GPIO 26/27)
 *
 * Free GPIO pins are configured as push-pull outputs that can be driven
 * high or low by the host tool.  They can also be reconfigured as inputs
 * and read back.
 *
 * Ignition (GPIO27) and Illumination (GPIO26) are 12/24 V digital inputs
 * on the board that are level-shifted to 3.3 V.  They are always inputs
 * with an internal pull-down.
 *
 * Pin map
 * ───────
 *  Free GPIO: 14, 15, 16, 17, 18, 19
 *  Illumination input: GPIO 26
 *  Ignition input:     GPIO 27
 */

/* Initialise all GPIOs. Call once from app_main. */
void gpio_module_init(void);

/*
 * Drive a free GPIO pin high (level=1) or low (level=0).
 * Returns 0 on success, -1 if pin is not in the free-GPIO set.
 */
int gpio_module_set(int pin, int level);

/*
 * Read the current logic level of a free GPIO pin.
 * Returns 0 or 1 on success, -1 if pin is not in the free-GPIO set.
 */
int gpio_module_get(int pin);

/* Read the Ignition input (GPIO 27).  Returns 0 or 1. */
int gpio_module_get_ignition(void);

/* Read the Illumination input (GPIO 26).  Returns 0 or 1. */
int gpio_module_get_illumination(void);
