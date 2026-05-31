# Module: I2C Sensors

**Source:** `firmware/components/i2c_module/`

## Overview

Two on-board devices on **I2C bus 0** — verified from schematic and hardware tests:

| Device | Address | Bus pins | Role |
|--------|---------|----------|------|
| QMI8658A (QST, 6-axis IMU) | 0x6A | SDA=GPIO31, SCL=GPIO32 | 3-axis accelerometer (+ gyro) |
| PCF85063ATL (NXP RTC) | 0x51 | SDA=GPIO31, SCL=GPIO32 | Real-time clock |

A third address (0x7E) was seen during the hardware scan and is likely the
CO5300 display controller, handled by `display_module`.

## Accelerometer — QMI8658A

- **WHO_AM_I** register 0x00 = `0x05` — confirmed on all 5 boards
- SA0 pin pulled high via `ACCEL_ADDR` net → I2C address 0x6A
- Configured at init: 125 Hz ODR, ±2 g full-scale (CTRL2 = 0x60)
- Enabled via CTRL7 = 0x01 (accelerometer enable bit)
- Output registers: AX_L/H at 0x35/0x36, AY at 0x37/0x38, AZ at 0x39/0x3A
- Sensitivity: 1/16384 g/LSB = 0.000598 (m/s²)/LSB

## RTC — PCF85063ATL

- Address 0x51, with 32.768 kHz crystal (X3 on schematic)
- **Note:** PCF85063ATL differs from PCF8563 — time registers start at **0x04**, not 0x02
- Register 0x03 is a general-purpose RAM byte (used in hardware test to verify R/W)
- Registers 0x04–0x0A: seconds, minutes, hours, days, weekdays, months, years (BCD)

## Public API

```c
int i2c_module_init(void);
int i2c_module_read_accel(float *x, float *y, float *z);  // m/s²
int i2c_module_read_rtc(rtc_time_t *t);
int i2c_module_set_rtc(const rtc_time_t *t);
```

## JSON commands

| Command | Parameters | Response |
|---------|-----------|----------|
| `i2c_read` | `sensor`: `"accel"` | `{status, cmd, sensor, x, y, z}` (m/s²) |
| `i2c_read` | `sensor`: `"rtc"` | `{status, cmd, sensor, year, month, day, hour, minute, second}` |
