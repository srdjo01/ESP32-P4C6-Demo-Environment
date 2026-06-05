# Module: eMMC Storage

**Source:** `firmware/components/emmc_module/`

## Overview

Tests the on-board eMMC via the **SDMMC peripheral** (8-line bus, GPIO-matrix
routed).  A known byte pattern is written, read back, and verified.  The bus
frequency is selectable so that reliability at each clock speed can be confirmed.

## Hardware

GPIO assignments verified from **SCH_Schematic_2026-05-11.pdf** (Page 7 — eMMC).
The eMMC (Samsung KLM8G1GETF-B041, 8 GB) is connected internally on the
Waveshare ESP32-P4 module via MMC_* nets.

| Signal | GPIO |
|--------|------|
| CLK    | 40   |
| CMD    | 39   |
| D0     | 45   |
| D1     | 46   |
| D2     | 47   |
| D3     | 43   |
| D4     | 44   |
| D5     | 42   |
| D6     | 41   |
| D7     | 48   |

**LDO channel 4** is enabled at **1.8 V** before every mount and released after.

## Supported frequencies

| kHz | Label |
|-----|-------|
| 400 | Init / slow |
| 4000 | 4 MHz |
| 10000 | 10 MHz |
| 20000 | 20 MHz (default) |
| 40000 | 40 MHz (high speed) — tested PASS on boards #1–5 |
| 52000 | 52 MHz (max for standard HS mode) |

## Test method

The test writes directly to raw eMMC sectors via `sdmmc_write_sectors` /
`sdmmc_read_sectors`, **bypassing the FAT filesystem entirely**. This tests the
eMMC hardware itself rather than a filesystem layer, and avoids corruption
issues from a stale or damaged FAT.

The test region starts at sector `0x100000` (~512 MB into the device), well
clear of any boot/partition structures. Each byte position `i` is filled with
`i & 0xFF` (repeating 0x00–0xFF), written in 32 KB blocks and verified
byte-by-byte on read.

## Public API

```c
int emmc_module_init(void);
int emmc_module_run_test(int freq_khz, int size_kb, emmc_test_result_t *result);
```

`result` contains `write_ok`, `read_ok`, `verify_ok`, and `duration_ms`.

## JSON command

```json
{"cmd": "emmc_test", "freq_khz": 40000, "size_kb": 64}
```

Response:
```json
{"status": "ok", "cmd": "emmc_test",
 "freq_khz": 40000, "size_kb": 64,
 "write_ok": true, "read_ok": true, "verify_ok": true,
 "duration_ms": 312}
```
