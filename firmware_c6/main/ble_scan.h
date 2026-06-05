#pragma once

#include <stdint.h>
#include <stdbool.h>

/* One discovered BLE device. */
typedef struct {
    char    name[32];   /* advertised name, or "(unknown)" */
    char    addr[18];   /* "AA:BB:CC:DD:EE:FF"             */
    int8_t  rssi;       /* signal strength in dBm          */
} ble_dev_t;

/*
 * Initialise the NimBLE host stack. Call once at boot, after the BT controller
 * is up. Returns 0 on success.
 */
int ble_scan_init(void);

/*
 * Run a passive BLE scan for `duration_ms` milliseconds. Discovered devices are
 * written to `out` (up to `max`, de-duplicated by address). Blocks until the
 * scan finishes. Returns the number of devices found, or -1 on error.
 */
int ble_scan_run(ble_dev_t *out, int max, int duration_ms);
