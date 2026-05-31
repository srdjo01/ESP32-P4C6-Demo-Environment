# Module: GPIO

**Source:** `firmware/components/gpio_module/`

## Overview

Manages two distinct GPIO groups:

| Group | Pins | Direction | Purpose |
|-------|------|-----------|---------|
| Free GPIO | 14, 15, 16, 17, 18, 19 | Output (readable) | General-purpose host-controlled I/O |
| Illumination | GPIO 26 | Input (pull-down) | 12/24 V digital input via level-shifter |
| Ignition | GPIO 27 | Input (pull-down) | 12/24 V digital input via level-shifter |

## Initialisation

`gpio_module_init()` is called once from `app_main()`.  It:
1. Resets and configures free GPIOs as `GPIO_MODE_INPUT_OUTPUT` push-pull, default LOW.
2. Configures GPIO 26 and 27 as inputs with internal pull-down.

## Public API

```c
void gpio_module_init(void);
int  gpio_module_set(int pin, int level);   // pin ∈ {14…19}, level 0/1
int  gpio_module_get(int pin);              // returns 0/1, or -1 if invalid pin
int  gpio_module_get_ignition(void);        // returns 0/1
int  gpio_module_get_illumination(void);    // returns 0/1
```

## JSON commands

| Command | Parameters | Response |
|---------|-----------|----------|
| `gpio_set` | `pin` (int), `level` (0/1) | `{status, cmd, pin, level}` |
| `gpio_get` | `pin` (int) | `{status, cmd, pin, level}` |
| `ign_ilum_get` | — | `{status, cmd, ignition, illumination}` |

## Notes

- Free GPIOs 14–19 are shared with the ESP32-C6 SDIO bus used for Wi-Fi.
  If ESP-Hosted is enabled, do **not** toggle these pins manually while Wi-Fi
  is active.
- The ignition/illumination signals are level-shifted externally; the GPIO
  sees 0–3.3 V only.
