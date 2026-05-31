# Module: Ignition & Illumination

**Source:** `firmware/components/gpio_module/` (same module as free GPIO)

## Overview

Two **12/24 V digital inputs** that indicate vehicle ignition and illumination
states.  The board uses an external level-shifter, so the ESP32-P4 GPIO sees
0–3.3 V only.

| Signal | GPIO | Pull |
|--------|------|------|
| Illumination | 26 | Pull-down |
| Ignition | 27 | Pull-down |

## Behaviour

- When no external voltage is applied: GPIO reads **0** (inactive).
- When 12 V or 24 V is applied to the input connector: level-shifter pulls
  GPIO to 3.3 V → reads **1** (active).

## Public API

```c
int gpio_module_get_ignition(void);      // returns 0 or 1
int gpio_module_get_illumination(void);  // returns 0 or 1
```

## JSON command

```json
{"cmd": "ign_ilum_get"}
```

Response:
```json
{"status": "ok", "cmd": "ign_ilum_get", "ignition": 0, "illumination": 1}
```
