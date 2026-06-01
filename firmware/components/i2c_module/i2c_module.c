#include "i2c_module.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "i2c_module";

/* ── Bus configuration — verified from SCH_Schematic_2026-05-11.pdf ────── */

#define I2C_PORT       I2C_NUM_0
#define I2C_SDA_GPIO   31
#define I2C_SCL_GPIO   32
#define I2C_SPEED_HZ   400000

/* ── Device addresses ───────────────────────────────────────────────────── */

#define ACCEL_ADDR   0x6A   /* QMI8658A, SA0 pin pulled high via ACCEL_ADDR net */
#define RTC_ADDR     0x51   /* PCF85063ATL */

/* ── QMI8658A register map (QST Corporation 6-axis IMU) ─────────────────
 *
 * WHO_AM_I at 0x00 returns 0x05 — verified on all 5 boards.
 *
 * Key registers:
 *   0x00  WHO_AM_I        → 0x05
 *   0x02  CTRL1           SPI/I2C config (leave default for I2C)
 *   0x03  CTRL2           Accelerometer: ODR + full-scale
 *   0x08  CTRL7           Sensor enable (bit0=accel, bit1=gyro)
 *   0x35  AX_L            Accelerometer X low byte
 *   0x36  AX_H            Accelerometer X high byte
 *   0x37  AY_L            Y low
 *   0x38  AY_H            Y high
 *   0x39  AZ_L            Z low
 *   0x3A  AZ_H            Z high
 *
 * CTRL2 encoding:
 *   bits [6:4] = AOdr (output data rate):
 *     0b110 = 125 Hz
 *   bits [3:1] = AFS (full-scale):
 *     0b000 = ±2 g  → sensitivity = 1/16384 g/LSB
 *   → CTRL2 = 0x60
 *
 * CTRL7: set bit 0 to enable accelerometer output.
 * Sensitivity at ±2 g, 16-bit: 1/16384 g/LSB = 0.061 mg/LSB
 *                               = 0.000598 (m/s²)/LSB
 */
#define QMI_REG_WHO_AM_I   0x00
#define QMI_EXPECTED_ID    0x05
#define QMI_REG_CTRL2      0x03
#define QMI_CTRL2_VAL      0x60   /* 125 Hz ODR, ±2 g */
#define QMI_REG_CTRL7      0x08
#define QMI_CTRL7_ACCEL_EN 0x01   /* enable accelerometer */
#define QMI_REG_AX_L       0x35
#define QMI_SENSITIVITY    0.000598f   /* m/s² per LSB (±2 g, 16-bit) */

/* ── PCF85063ATL register map (NXP RTC) ─────────────────────────────────
 *
 * Address 0x51.  Register 0x03 is a general-purpose RAM byte (R/W),
 * used in the hardware test to confirm read/write access.
 *
 * Time registers (BCD format unless noted):
 *   0x04  VL_seconds  bit7=VL voltage-low flag; bits[6:0] = seconds BCD
 *   0x05  Minutes     bits[6:0] = minutes BCD
 *   0x06  Hours       bits[5:0] = hours BCD (24-hour mode)
 *   0x07  Days        bits[5:0] = days BCD (1–31)
 *   0x08  Weekdays    bits[2:0] = weekday (0–6)
 *   0x09  Months      bits[4:0] = months BCD (1–12)
 *   0x0A  Years       bits[7:0] = years BCD (00–99, offset 2000)
 *
 * Note: PCF85063ATL differs from PCF8563 — time registers start at 0x04,
 * not 0x02.
 */
#define RTC_REG_CTRL1    0x00
#define RTC_REG_SECONDS  0x04
#define RTC_REGS_COUNT   7   /* seconds (0x04) through years (0x0A) */

/* ── Internal state ─────────────────────────────────────────────────────── */

static i2c_master_bus_handle_t  s_bus    = NULL;
static i2c_master_dev_handle_t  s_accel  = NULL;
static i2c_master_dev_handle_t  s_rtc    = NULL;
static bool s_init = false;

/* ── Low-level helpers ──────────────────────────────────────────────────── */

static uint8_t bcd_to_bin(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0f); }
static uint8_t bin_to_bcd(uint8_t bin) { return ((bin / 10) << 4) | (bin % 10); }

static esp_err_t reg_read(i2c_master_dev_handle_t dev,
                          uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 100);
}

static esp_err_t reg_write(i2c_master_dev_handle_t dev,
                           uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[17];
    if (len > 16) return ESP_ERR_INVALID_ARG;
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(dev, buf, len + 1, 100);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int i2c_module_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = I2C_PORT,
        .sda_io_num             = I2C_SDA_GPIO,
        .scl_io_num             = I2C_SCL_GPIO,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus");
        return -1;
    }

    i2c_device_config_t accel_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ACCEL_ADDR,
        .scl_speed_hz    = I2C_SPEED_HZ,
    };
    i2c_device_config_t rtc_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = RTC_ADDR,
        .scl_speed_hz    = I2C_SPEED_HZ,
    };
    if (i2c_master_bus_add_device(s_bus, &accel_cfg, &s_accel) != ESP_OK ||
        i2c_master_bus_add_device(s_bus, &rtc_cfg,   &s_rtc)   != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C devices");
        return -1;
    }

    /* Verify QMI8658A WHO_AM_I. */
    uint8_t who = 0;
    if (reg_read(s_accel, QMI_REG_WHO_AM_I, &who, 1) != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658A not responding at 0x%02X", ACCEL_ADDR);
        return -1;
    }
    if (who != QMI_EXPECTED_ID) {
        ESP_LOGW(TAG, "QMI8658A WHO_AM_I = 0x%02X (expected 0x%02X)",
                 who, QMI_EXPECTED_ID);
    } else {
        ESP_LOGI(TAG, "QMI8658A detected (WHO_AM_I = 0x%02X)", who);
    }

    /* Soft-reset the QMI8658A so it starts from a known state. */
    uint8_t reset_cmd = 0xB0;
    reg_write(s_accel, 0x0A, &reset_cmd, 1);   /* CTRL9 = soft reset */
    vTaskDelay(pdMS_TO_TICKS(50));              /* wait for reboot    */

    /* Re-verify WHO_AM_I after reset. */
    reg_read(s_accel, QMI_REG_WHO_AM_I, &who, 1);
    ESP_LOGI(TAG, "Post-reset WHO_AM_I = 0x%02X", who);

    /* Configure accelerometer: 125 Hz ODR, ±2 g full-scale. */
    uint8_t ctrl2 = QMI_CTRL2_VAL;
    if (reg_write(s_accel, QMI_REG_CTRL2, &ctrl2, 1) != ESP_OK) {
        ESP_LOGW(TAG, "CTRL2 write failed");
    }

    /* Enable accelerometer (bit0) + gyro (bit1) — some variants need both. */
    uint8_t ctrl7 = 0x03;
    if (reg_write(s_accel, QMI_REG_CTRL7, &ctrl7, 1) != ESP_OK) {
        ESP_LOGW(TAG, "CTRL7 write failed");
    }

    /* Read back CTRL7 and a few data registers for diagnostics. */
    uint8_t ctrl7_rb = 0;
    reg_read(s_accel, QMI_REG_CTRL7, &ctrl7_rb, 1);
    ESP_LOGI(TAG, "QMI8658A CTRL7 readback = 0x%02X (expected 0x03)", ctrl7_rb);

    /* Wait 3 ODR cycles (25 ms) for first sample to be ready. */
    vTaskDelay(pdMS_TO_TICKS(25));

    /* Verify PCF85063ATL responds. */
    uint8_t ctrl = 0;
    if (reg_read(s_rtc, RTC_REG_CTRL1, &ctrl, 1) != ESP_OK) {
        ESP_LOGE(TAG, "PCF85063ATL not responding at 0x%02X", RTC_ADDR);
        return -1;
    }

    s_init = true;
    ESP_LOGI(TAG, "I2C module ready — QMI8658A + PCF85063ATL "
             "(SDA=GPIO%d SCL=GPIO%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);
    return 0;
}

int i2c_module_read_accel(float *x, float *y, float *z)
{
    if (!s_init) return -1;

    /* Read 6 bytes starting at AX_L (0x35). */
    uint8_t raw[6];
    if (reg_read(s_accel, QMI_REG_AX_L, raw, 6) != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658A read failed");
        return -1;
    }
    ESP_LOGD(TAG, "raw[6]: %02X %02X %02X %02X %02X %02X",
             raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);

    /* QMI8658A output is little-endian, two's complement 16-bit. */
    int16_t rx = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ry = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t rz = (int16_t)((raw[5] << 8) | raw[4]);

    *x = rx * QMI_SENSITIVITY;
    *y = ry * QMI_SENSITIVITY;
    *z = rz * QMI_SENSITIVITY;
    return 0;
}

int i2c_module_read_rtc(rtc_time_t *t)
{
    if (!s_init) return -1;

    /* Read 7 bytes from 0x04 (VL_seconds) through 0x0A (Years). */
    uint8_t regs[RTC_REGS_COUNT];
    if (reg_read(s_rtc, RTC_REG_SECONDS, regs, RTC_REGS_COUNT) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063ATL read failed");
        return -1;
    }

    t->second  = bcd_to_bin(regs[0] & 0x7f);  /* mask VL flag */
    t->minute  = bcd_to_bin(regs[1] & 0x7f);
    t->hour    = bcd_to_bin(regs[2] & 0x3f);
    t->day     = bcd_to_bin(regs[3] & 0x3f);
    t->weekday = regs[4] & 0x07;
    t->month   = bcd_to_bin(regs[5] & 0x1f);
    t->year    = bcd_to_bin(regs[6]);
    return 0;
}

int i2c_module_set_rtc(const rtc_time_t *t)
{
    if (!s_init) return -1;

    uint8_t regs[RTC_REGS_COUNT] = {
        bin_to_bcd(t->second),
        bin_to_bcd(t->minute),
        bin_to_bcd(t->hour),
        bin_to_bcd(t->day),
        t->weekday & 0x07,
        bin_to_bcd(t->month),
        bin_to_bcd(t->year),
    };
    if (reg_write(s_rtc, RTC_REG_SECONDS, regs, RTC_REGS_COUNT) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063ATL write failed");
        return -1;
    }
    return 0;
}
