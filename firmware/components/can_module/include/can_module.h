#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * can_module — CAN bus over TJA1051 transceiver
 *
 * Wiring (ESP32-P4 ↔ TJA1051)
 * ───────────────────────────
 *   ESP32-P4 GPIO1  →  TJA1051 TXD
 *   ESP32-P4 GPIO2  ←  TJA1051 RXD
 *   ESP32-P4 GPIO3  →  TJA1051 S / STB   (low = normal mode, high = silent/standby)
 *   ESP32-P4 GND    ↔  TJA1051 GND
 *
 * The driver uses ESP-IDF's TWAI peripheral. Default mode pulls STB low so the
 * transceiver is in normal (active) mode; can_module_set_silent(true) puts the
 * transceiver into silent mode without touching the TWAI controller.
 */

typedef struct {
    bool     started;          /* TWAI has been started */
    bool     silent;           /* STB pin is high (transceiver in silent mode) */
    uint32_t bitrate;          /* configured bitrate, bits/sec */
    uint32_t tx_msgs;          /* messages successfully transmitted */
    uint32_t rx_msgs;          /* messages successfully received */
    uint32_t tx_errors;        /* TWAI tx error counter */
    uint32_t rx_errors;        /* TWAI rx error counter */
    uint32_t bus_errors;       /* TWAI bus_error_count */
    uint32_t arb_lost;         /* arbitration_lost_count */
    bool     bus_off;          /* node entered bus-off state */
} can_module_status_t;

typedef struct {
    uint32_t id;               /* 11-bit standard, or 29-bit extended */
    bool     extended;         /* true → 29-bit identifier */
    bool     rtr;              /* true → remote-transmission-request frame */
    uint8_t  dlc;              /* 0..8 */
    uint8_t  data[8];
} can_frame_t;

/* Initialise GPIOs (does not start the bus). Call once from app_main. */
void can_module_init(void);

/*
 * Start the TWAI driver at the given bitrate (bits/sec). Common values:
 * 1000000, 800000, 500000, 250000, 125000, 100000, 50000, 25000.
 * Returns 0 on success, -1 on invalid bitrate, -2 on driver error.
 */
int can_module_start(uint32_t bitrate);

/* Stop and uninstall the TWAI driver. Returns 0 on success. */
int can_module_stop(void);

/*
 * Drive the TJA1051 STB pin. silent=true puts the transceiver into silent
 * mode (TX disabled, RX still works). Persists across start/stop.
 */
void can_module_set_silent(bool silent);

/*
 * Transmit a frame. timeout_ms applies to enqueueing the frame in the TWAI
 * tx queue (does not wait for actual ack).
 * Returns 0 on success, -1 if the bus has not been started, -2 on tx queue
 * full / timeout.
 */
int can_module_send(const can_frame_t *frame, int timeout_ms);

/*
 * Receive a frame. Blocks for at most timeout_ms milliseconds.
 * Returns 1 if a frame was received, 0 on timeout, -1 if not started.
 */
int can_module_recv(can_frame_t *frame, int timeout_ms);

/* Read current status / counters. Returns 0 on success. */
int can_module_get_status(can_module_status_t *out);

/*
 * Run a self-test in TWAI no-ack/loopback mode: no transceiver required, and
 * the controller acks its own frames. Returns 0 if a frame was sent and
 * looped back successfully, -1 otherwise. Restores the previous bitrate /
 * mode on exit.
 */
int can_module_self_test(uint32_t bitrate);
