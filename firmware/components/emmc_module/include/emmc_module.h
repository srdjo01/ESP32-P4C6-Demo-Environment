#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * emmc_module — eMMC storage test
 *
 * Mounts the eMMC via the SDMMC peripheral (8-line bus, GPIO-matrix routed),
 * writes a known test pattern, reads it back, and verifies data integrity.
 * The host selects the bus frequency so that reliability at each clock speed
 * can be confirmed.
 *
 * LDO channel 4 is enabled at 1.8 V before every mount and released after
 * unmount (same requirement as the ESP-IDF eMMC example).
 *
 * Supported frequencies (kHz): 400, 4000, 10000, 20000, 40000, 52000
 */

/* Result structure filled by emmc_module_run_test(). */
typedef struct {
    bool     write_ok;    /* Pattern write succeeded */
    bool     read_ok;     /* Read-back succeeded */
    bool     verify_ok;   /* Read data matches written pattern */
    uint32_t duration_ms; /* Total test duration in milliseconds */
} emmc_test_result_t;

/*
 * One-time module initialisation (verifies the SDMMC peripheral is accessible).
 * Returns 0 on success, -1 on error.
 */
int emmc_module_init(void);

/*
 * Run the full write-read-verify cycle at the given frequency.
 *
 *  freq_khz  — bus clock in kHz (clamped to 400–52000)
 *  size_kb   — amount of data to write/read in KB (clamped to 1–4096)
 *  result    — filled on return (always, even on partial failure)
 *
 * Returns 0 on success (all three flags true), -1 on fatal error.
 */
int emmc_module_run_test(int freq_khz, int size_kb, emmc_test_result_t *result);
