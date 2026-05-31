#include "emmc_module.h"

#include <string.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "emmc_module";

#define MOUNT_POINT  "/eMMC"
#define TEST_FILE    MOUNT_POINT "/p4c6_test.bin"

/* LDO channel 4 powers the eMMC 1.8 V I/O — must stay enabled during use. */
#define LDO_CHANNEL   4
#define LDO_VOLTAGE   1800

/*
 * eMMC SDMMC GPIO map — verified from SCH_Schematic_2026-05-11.pdf.
 * The eMMC (U9, KLM8G1GETF-B041) is connected internally on the
 * Waveshare ESP32-P4 module via the MMC_* nets.
 */
#define EMMC_CLK_GPIO  41
#define EMMC_CMD_GPIO  40
#define EMMC_D0_GPIO   46
#define EMMC_D1_GPIO   47
#define EMMC_D2_GPIO   48
#define EMMC_D3_GPIO   44
#define EMMC_D4_GPIO   45
#define EMMC_D5_GPIO   43
#define EMMC_D6_GPIO   42
#define EMMC_D7_GPIO   49

/* Test pattern: repeating 0x00–0xFF sequence. */
#define PATTERN_CHUNK  512

static bool s_init = false;

/* ── Internal helpers ────────────────────────────────────────────────────── */

static esp_ldo_channel_handle_t acquire_ldo(void)
{
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t cfg = {
        .chan_id    = LDO_CHANNEL,
        .voltage_mv = LDO_VOLTAGE,
    };
    if (esp_ldo_acquire_channel(&cfg, &ldo) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire LDO ch%d", LDO_CHANNEL);
        return NULL;
    }
    return ldo;
}

static int mount_emmc(int freq_khz, sdmmc_card_t **card_out)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = freq_khz;

    sdmmc_slot_config_t slot = {
        .cd    = SDMMC_SLOT_NO_CD,
        .wp    = SDMMC_SLOT_NO_WP,
        .width = 8,
        .clk   = EMMC_CLK_GPIO,
        .cmd   = EMMC_CMD_GPIO,
        .d0    = EMMC_D0_GPIO,
        .d1    = EMMC_D1_GPIO,
        .d2    = EMMC_D2_GPIO,
        .d3    = EMMC_D3_GPIO,
        .d4    = EMMC_D4_GPIO,
        .d5    = EMMC_D5_GPIO,
        .d6    = EMMC_D6_GPIO,
        .d7    = EMMC_D7_GPIO,
    };
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot,
                                             &mount_cfg, card_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed at %d kHz: %s", freq_khz, esp_err_to_name(ret));
        return -1;
    }
    sdmmc_card_print_info(stdout, *card_out);
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int emmc_module_init(void)
{
    /* Quick probe: try mounting at 20 MHz and immediately unmount. */
    esp_ldo_channel_handle_t ldo = acquire_ldo();
    if (!ldo) return -1;

    sdmmc_card_t *card = NULL;
    int rc = mount_emmc(20000, &card);
    if (rc == 0) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        s_init = true;
        ESP_LOGI(TAG, "eMMC probe OK");
    }
    esp_ldo_release_channel(ldo);
    return rc;
}

int emmc_module_run_test(int freq_khz, int size_kb, emmc_test_result_t *result)
{
    memset(result, 0, sizeof(*result));

    /* Clamp parameters. */
    if (freq_khz < 400)  freq_khz = 400;
    if (freq_khz > 52000) freq_khz = 52000;
    if (size_kb < 1)    size_kb = 1;
    if (size_kb > 4096) size_kb = 4096;

    int64_t t_start = esp_timer_get_time();

    esp_ldo_channel_handle_t ldo = acquire_ldo();
    if (!ldo) return -1;

    sdmmc_card_t *card = NULL;
    if (mount_emmc(freq_khz, &card) != 0) {
        esp_ldo_release_channel(ldo);
        return -1;
    }

    /* ── Write phase ── */
    FILE *f = fopen(TEST_FILE, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open test file for writing");
        goto cleanup;
    }

    uint8_t chunk[PATTERN_CHUNK];
    size_t total_bytes = (size_t)size_kb * 1024;
    bool write_ok = true;

    for (size_t written = 0; written < total_bytes; written += PATTERN_CHUNK) {
        size_t to_write = (total_bytes - written < PATTERN_CHUNK)
                          ? (total_bytes - written) : PATTERN_CHUNK;
        for (size_t j = 0; j < to_write; j++) {
            chunk[j] = (uint8_t)((written + j) & 0xFF);
        }
        if (fwrite(chunk, 1, to_write, f) != to_write) {
            write_ok = false;
            break;
        }
    }
    fclose(f);
    result->write_ok = write_ok;

    if (!write_ok) goto cleanup;

    /* ── Read-back and verify phase ── */
    f = fopen(TEST_FILE, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open test file for reading");
        goto cleanup;
    }

    result->read_ok   = true;
    result->verify_ok = true;
    size_t verified   = 0;

    while (verified < total_bytes) {
        size_t to_read = (total_bytes - verified < PATTERN_CHUNK)
                         ? (total_bytes - verified) : PATTERN_CHUNK;
        size_t got = fread(chunk, 1, to_read, f);
        if (got != to_read) {
            result->read_ok = false;
            break;
        }
        for (size_t j = 0; j < got; j++) {
            if (chunk[j] != (uint8_t)((verified + j) & 0xFF)) {
                result->verify_ok = false;
                break;
            }
        }
        if (!result->verify_ok) break;
        verified += got;
    }
    fclose(f);

    /* Remove test file. */
    unlink(TEST_FILE);

cleanup:
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    esp_ldo_release_channel(ldo);

    result->duration_ms = (uint32_t)((esp_timer_get_time() - t_start) / 1000);
    ESP_LOGI(TAG, "eMMC test @ %d kHz, %d KB: write=%d read=%d verify=%d (%lu ms)",
             freq_khz, size_kb,
             result->write_ok, result->read_ok, result->verify_ok,
             result->duration_ms);
    return (result->write_ok && result->read_ok && result->verify_ok) ? 0 : -1;
}
