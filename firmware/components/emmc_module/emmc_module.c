#include "emmc_module.h"

#include <string.h>
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_ldo_regulator.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "emmc_module";

/* LDO channel 4 powers the eMMC 1.8 V I/O — must stay enabled during use. */
#define LDO_CHANNEL   4
#define LDO_VOLTAGE   1800

/*
 * eMMC SDMMC GPIO map — verified from SCH_Schematic_2026-05-11.pdf page 7
 * and the working ESP32P4Dev/Test Examples/P4/emmc reference.
 */
#define EMMC_CLK_GPIO  40
#define EMMC_CMD_GPIO  39
#define EMMC_D0_GPIO   45
#define EMMC_D1_GPIO   46
#define EMMC_D2_GPIO   47
#define EMMC_D3_GPIO   43
#define EMMC_D4_GPIO   44
#define EMMC_D5_GPIO   42
#define EMMC_D6_GPIO   41
#define EMMC_D7_GPIO   48

/*
 * Raw sector test region. We bypass FAT entirely and read/write sectors
 * directly via the SDMMC driver — this tests the eMMC hardware itself, not
 * a filesystem layer. START_SECTOR sits ~512 MB into the device, far from
 * any boot/partition structures at the start of the card.
 */
#define SECTOR_SIZE    512
#define START_SECTOR   0x100000          /* 1,048,576 → ~512 MB offset       */
#define BLOCK_SECTORS  64                /* 32 KB per DMA transfer           */
#define BLOCK_BYTES    (BLOCK_SECTORS * SECTOR_SIZE)

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
    /* Allow eMMC VCCQ to stabilise before first command. */
    vTaskDelay(pdMS_TO_TICKS(100));
    return ldo;
}

/*
 * Initialise the eMMC card (no filesystem). Fills *card and leaves the SDMMC
 * host running; caller must sdmmc_host_deinit() when done. Returns 0 on ok.
 */
static int init_card(int freq_khz, sdmmc_card_t *card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = freq_khz;
    host.flags        = SDMMC_HOST_FLAG_8BIT | SDMMC_HOST_FLAG_DDR;

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

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init failed: %s", esp_err_to_name(err));
        return -1;
    }
    /* Use host.slot (default SLOT_1) — slot 1 supports GPIO-matrix routing,
     * which these non-IOMUX pins require. Slot 0 is dedicated-IOMUX only. */
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init_slot failed: %s", esp_err_to_name(err));
        sdmmc_host_deinit();
        return -1;
    }
    err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "card_init failed at %d kHz: %s", freq_khz, esp_err_to_name(err));
        sdmmc_host_deinit();
        return -1;
    }
    sdmmc_card_print_info(stdout, card);
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int emmc_module_init(void)
{
    esp_ldo_channel_handle_t ldo = acquire_ldo();
    if (!ldo) return -1;

    sdmmc_card_t card;
    int rc = init_card(20000, &card);
    if (rc == 0) {
        sdmmc_host_deinit();
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

    sdmmc_card_t card;
    if (init_card(freq_khz, &card) != 0) {
        esp_ldo_release_channel(ldo);
        result->duration_ms = (uint32_t)((esp_timer_get_time() - t_start) / 1000);
        return -1;
    }

    /* DMA-capable transfer buffer (one block at a time). */
    uint8_t *buf = heap_caps_malloc(BLOCK_BYTES, MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "Cannot allocate DMA buffer");
        goto cleanup;
    }

    size_t total_sectors = ((size_t)size_kb * 1024 + SECTOR_SIZE - 1) / SECTOR_SIZE;

    /* ── Write phase ── */
    bool write_ok = true;
    for (size_t sec = 0; sec < total_sectors; sec += BLOCK_SECTORS) {
        size_t n = (total_sectors - sec < BLOCK_SECTORS)
                   ? (total_sectors - sec) : BLOCK_SECTORS;
        for (size_t i = 0; i < n * SECTOR_SIZE; i++) {
            buf[i] = (uint8_t)((sec * SECTOR_SIZE + i) & 0xFF);
        }
        if (sdmmc_write_sectors(&card, buf, START_SECTOR + sec, n) != ESP_OK) {
            write_ok = false;
            break;
        }
    }
    result->write_ok = write_ok;
    if (!write_ok) {
        ESP_LOGE(TAG, "sdmmc_write_sectors failed");
        goto free_cleanup;
    }

    /* ── Read-back and verify phase ── */
    result->read_ok   = true;
    result->verify_ok = true;
    for (size_t sec = 0; sec < total_sectors; sec += BLOCK_SECTORS) {
        size_t n = (total_sectors - sec < BLOCK_SECTORS)
                   ? (total_sectors - sec) : BLOCK_SECTORS;
        memset(buf, 0, n * SECTOR_SIZE);
        if (sdmmc_read_sectors(&card, buf, START_SECTOR + sec, n) != ESP_OK) {
            result->read_ok = false;
            break;
        }
        for (size_t i = 0; i < n * SECTOR_SIZE; i++) {
            if (buf[i] != (uint8_t)((sec * SECTOR_SIZE + i) & 0xFF)) {
                result->verify_ok = false;
                break;
            }
        }
        if (!result->verify_ok) break;
    }

free_cleanup:
    free(buf);

cleanup:
    sdmmc_host_deinit();
    esp_ldo_release_channel(ldo);

    result->duration_ms = (uint32_t)((esp_timer_get_time() - t_start) / 1000);
    ESP_LOGI(TAG, "eMMC test @ %d kHz, %d KB: write=%d read=%d verify=%d (%lu ms)",
             freq_khz, size_kb,
             result->write_ok, result->read_ok, result->verify_ok,
             result->duration_ms);
    return (result->write_ok && result->read_ok && result->verify_ok) ? 0 : -1;
}
