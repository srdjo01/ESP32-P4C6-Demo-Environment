#pragma once

#include <stdint.h>

/*
 * i2c_module — I2C sensors on I2C bus 0 (SDA = GPIO31, SCL = GPIO32)
 *
 * Devices — verified from SCH_Schematic_2026-05-11.pdf and hardware tests
 * ──────────────────────────────────────────────────────────────────────────
 *  Accelerometer  address 0x6A   QMI8658A (QST Corporation 6-axis IMU)
 *                 SA0 pulled high via ACCEL_ADDR net → address 0x6A
 *                 WHO_AM_I reg 0x00 → 0x05  (verified on all 5 boards)
 *
 *  RTC            address 0x51   PCF85063ATL (NXP)
 *                 Time registers start at 0x04 (differs from PCF8563)
 *
 * Note: a third device was detected at address 0x7E during the scan — this is
 * likely the CO5300 display controller being partially visible on the shared
 * I2C bus.  It is handled by display_module, not here.
 */

/* Time structure returned by i2c_module_read_rtc. Year is offset from 2000. */
typedef struct {
    uint8_t second;   /* 0–59 */
    uint8_t minute;   /* 0–59 */
    uint8_t hour;     /* 0–23 */
    uint8_t day;      /* 1–31 */
    uint8_t weekday;  /* 0–6  */
    uint8_t month;    /* 1–12 */
    uint8_t year;     /* 0–99 (add 2000 for full year) */
} rtc_time_t;

/*
 * Initialise I2C bus 0 and verify that both devices respond.
 * Returns 0 on success, -1 if one or both devices cannot be reached.
 */
int i2c_module_init(void);

/*
 * Read accelerometer X/Y/Z in m/s² (full-scale ±2 g, 16-bit signed).
 * Returns 0 on success, -1 on I2C error.
 */
int i2c_module_read_accel(float *x, float *y, float *z);

/*
 * Read the current time from the PCF8563 RTC.
 * Returns 0 on success, -1 on I2C error.
 */
int i2c_module_read_rtc(rtc_time_t *t);

/*
 * Set the time on the PCF8563 RTC.
 * Returns 0 on success, -1 on I2C error.
 */
int i2c_module_set_rtc(const rtc_time_t *t);
